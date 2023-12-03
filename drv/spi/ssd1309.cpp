/*
 *  Copyright (C) 2022-2023, Thomas Maier-Komor
 *  Atrium Firmware Package for ESP
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sdkconfig.h>

#ifdef CONFIG_SSD1309

#include "ssd1309.h"
#include "log.h"
#include "profiling.h"

#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if IDF_VERSION >= 50
#define ets_delay_us esp_rom_delay_us
#endif

#define CHAR_WIDTH	6
#define CHAR_HEIGHT	8

#define CTRL_CMD1	0x00	// command and data
#define CTRL_CMDN	0x80	// command with more commands
#define CTRL_CMDC	0xc0	// continuation command
#define CTRL_DATA	0x00	// data only

#define CMD_NOP		0xe3

#define TAG MODULE_SSD130X

#if 1
#define log_devel log_dbug
#else
#define log_devel(...)
#endif

SSD1309 *SSD1309::Instance = 0;


static IRAM_ATTR void ssd1309_preop(spi_transaction_t *t)
{
	if (ssd1309_trans_t *a = (ssd1309_trans_t *) t->user) {
		if (a->set)
			if (esp_err_t e = gpio_set_level(a->gpio,a->lvl))
				log_warn(TAG,"set DC: %s",esp_err_to_name(e));
	}
}


static IRAM_ATTR void ssd1309_postop(spi_transaction_t *t)
{
	if (ssd1309_trans_t *a = (ssd1309_trans_t *) t->user) {
		if (a->sem)
			xSemaphoreGive(a->sem);
	}
}


SSD1309::SSD1309(spi_host_t host, int8_t cs, uint8_t dc, int8_t reset, struct spi_device_t *hdl)
: SpiDevice(drvName(), cs)
, m_dc((gpio_num_t)dc)
, m_reset((gpio_num_t)reset)
{
#ifndef CONFIG_IDF_TARGET_ESP8266
	m_hdl = hdl;
#endif
	gpio_set_level(m_reset,1);
	gpio_set_level(m_dc,0);
}


#ifdef CONFIG_IDF_TARGET_ESP8266
SSD1309 *SSD1309::create(spi_host_device_t host, int8_t cs, int8_t dc, int8_t reset)
#else
SSD1309 *SSD1309::create(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t dc, int8_t reset)
#endif
{
	if (Instance) {
		log_warn(TAG,"instance already exists");
		return Instance;
	}
	if ((dc < 0) || (reset < 0))
		return 0;
	int8_t cs = cfg.spics_io_num;
	if (esp_err_t e = gpio_set_direction((gpio_num_t)dc,GPIO_MODE_OUTPUT)) {
		log_warn(TAG,"set DC/gpio%d to output: %s",dc,esp_err_to_name(e));
		return 0;
	}
	gpio_set_level((gpio_num_t)dc,1);
	if (esp_err_t e = gpio_set_pull_mode((gpio_num_t)reset,GPIO_PULLUP_ONLY)) {
		log_warn(TAG,"set RST/gpio%d pull-up: %s",reset,esp_err_to_name(e));
		return 0;
	}
	if (esp_err_t e = gpio_set_direction((gpio_num_t)reset,GPIO_MODE_OUTPUT)) {
		log_warn(TAG,"set RST/gpio%d to output: %s",reset,esp_err_to_name(e));
		return 0;
	}
	if (esp_err_t e = gpio_set_direction((gpio_num_t)cs,GPIO_MODE_OUTPUT)) {
		log_warn(TAG,"set CS/gpio%d to output: %s",cs,esp_err_to_name(e));
		return 0;
	}
#ifdef CONFIG_IDF_TARGET_ESP8266
	void *hdl = 0;
#else
	cfg.mode = 2;
	cfg.command_bits = 0;
	cfg.address_bits = 0;
	cfg.cs_ena_pretrans = 0;
	if (cfg.clock_speed_hz == 0)
		cfg.clock_speed_hz = SPI_MASTER_FREQ_8M;
	cfg.queue_size = 8;
	cfg.pre_cb = ssd1309_preop;
	cfg.post_cb = ssd1309_postop;
	spi_device_handle_t hdl;
	if (esp_err_t e = spi_bus_add_device(host,&cfg,&hdl))
		log_warn(TAG,"device add failed: %s",esp_err_to_name(e));
	else
#endif
		Instance = new SSD1309(host, cs, dc, reset, hdl);
	return Instance;
}


int SSD1309::init(uint16_t maxx, uint16_t maxy, uint8_t hwcfg)
{
	log_info(TAG,"init(%u,%u)",maxx,maxy);
	if ((maxx > 128) || (maxx < 1)) {
		log_warn(TAG,"invalid x resolution");
		return 1;
	}
	gpio_set_level(m_dc,1);
	if (m_reset >= 0) {
		gpio_set_level(m_reset,1);
		ets_delay_us(100);
		vTaskDelay(2);
		gpio_set_level(m_reset,0);
		ets_delay_us(100);
		vTaskDelay(2);
		gpio_set_level(m_reset,1);
		ets_delay_us(100);
		vTaskDelay(2);
	}
	m_width = maxx;
	m_height = maxy;
	uint32_t dsize = maxx * (maxy>>3);
	m_disp = (uint8_t *) malloc(dsize); // two dimensional array of n pages each of n columns.
	if (m_disp == 0) {
		log_error(TAG,"Out of memory.");
		return 1;
	}
	static const uint8_t setup[] = {
		0xae,					// display off
		0x00, 0x10,				// low CSA in PAM
		0x20, 0x00,				// address mode: horizontal
		0x81, 0x80,				// medium contrast
		0xa6,					// normal mode, a7=inverse
		0xa8, (uint8_t)(m_height-1),		// MUX
		0xd3, 0x00,				// display offset (optional)
		0xd5, 0x80,				// oszi freq (default), clock div=1 (optional)
		0xd9, 0x22,				// default pre-charge (optional)
		0xda, 0x12,					// COM hardware config
			(uint8_t) ((hwcfg&(hwc_rlmap|hwc_altm))|0x2),	
		0xa4,					// output RAM
		0xaf,					// display on

		/*
		0x40,					// display start line	(optional)
		0x8d, 0x14,				// enable charge pump
//		0x20, 0x02,				// address mode: page
		0xa0,					// map address 0 to seg0
		(uint8_t) (0xc0 | (hwcfg&hwc_iscan)),	// scan 0..n
		0x21, 0x0, 0x7f,			// column address range
		0x22, 0x0, 0x7,				// page address range
		0x2e,					// no scrolling
		*/
	};

	writeBytes(setup,sizeof(setup),pre_c);
	clear();
	flush();
	initOK();
	log_info(TAG,"ready");
	return 0;
}

int SSD1309::setOn(bool on)
{
	log_dbug(TAG,"setOn(%d)",on);
	uint8_t d[] = { (uint8_t)(0xae|on) };
	return writeBytes(d,sizeof(d),pre_c);
}

int SSD1309::setInvert(bool inv)
{
	log_dbug(TAG,"invert(%d)",inv);
	uint8_t d[] = { (uint8_t)(0xa6|inv) };
	return writeBytes(d,sizeof(d),pre_c);
}


int SSD1309::setBrightness(uint8_t contrast)
{
	uint8_t d[] = {0x81,contrast};
	return writeBytes(d,sizeof(d),pre_c);
}


void SSD1309::flush()
{
	if (m_dirty == 0)
		return;
	PROFILE_FUNCTION();
	uint8_t cmd[] = { 0x00, 0x10, 0xb0, };
	uint8_t numpg = m_height / 8 + ((m_height & 7) != 0);
	unsigned pgs = m_width;
	if (pgs == 128) {
		if (m_dirty == 0xff) {
			writeBytes(cmd,sizeof(cmd),pre_c);
			writeBytes(m_disp,m_width*8,pre_d);
			log_dbug(TAG,"sync 0-7");
			m_dirty = 0;
		} else if (m_dirty == 0xf) {
			writeBytes(cmd,sizeof(cmd),pre_c);
			writeBytes(m_disp,m_width*4,pre_d);
			log_dbug(TAG,"sync 0-3");
			m_dirty = 0;
		}
	}
	if (m_dirty) {
		uint8_t p = 0;
		while (p < numpg) {
			uint8_t f = p;
			unsigned n = 0;
			while (m_dirty & (1<<p)) {
				++n;
				++p;
			}
			if (n) {
				cmd[2] = 0xb0 + f;
				writeBytes(cmd,sizeof(cmd),pre_c);
				writeBytes(m_disp+f*pgs,n*pgs,pre_d);
				log_dbug(TAG,"sync %u-%u",f,p);
			}
			++p;
		}
		m_dirty = 0;
	}
}



int SSD1309::drawBits(uint16_t x, uint16_t y, uint8_t b, uint8_t n)
{
	static const uint8_t masks[] = {0,0x1,0x3,0x7,0xf,0x1f,0x3f,0x7f};
	b &= masks[n];
//	log_dbug(TAG,"drawBits(%u,%u,%x,%u)",x,y,b,n);
	uint8_t pg = y >> 3;
	unsigned off = pg * m_width + x;
	uint8_t shl = y & 7;
	uint16_t b0 = (uint16_t)b << shl;
	uint8_t b1 = (uint8_t)(b0 >> 8);
	if (b1)
		m_disp[off+m_width] |= b1;
	m_disp[off] |= (uint8_t)(b0&0xff);
//	log_dbug(TAG,"drawBits %x at %u",b0,off);
	return 0;
}


int SSD1309::drawByte(uint16_t x, uint16_t y, uint8_t b)
{
	uint8_t pg = y >> 3;
	uint16_t idx = pg * m_width + x;
	log_devel(TAG,"drawByte(%u,%u,%u)",x,y,b);
	if ((x >= m_width) || (y >= m_height)) {
		log_dbug(TAG,"off display %u,%u=%u pg=%u",(unsigned)x,(unsigned)y,(unsigned)idx,(unsigned)pg);
		return 1;
	}
	uint8_t shift = y & 7;
	if (shift != 0) {
		uint16_t idx2 = idx + m_width;
		if (idx2 >= (m_width*m_height))
			return 1;
		m_dirty |= 1<<(pg+1);
		uint16_t w = (uint16_t) b << shift;
		uint16_t m = 0xff << shift;
		m = ~m;
		uint8_t b0 = (m_disp[idx] & m) | (w & 0xFF);
		if (b0 != m_disp[idx]) {
			m_disp[idx] = b0;
			m_dirty |= 1<<pg;
		}
		b = (m_disp[idx2] & (m >> 8)) | (w >> 8);
		idx = idx2;
	}
	if (m_disp[idx] != b) {
		m_dirty |= 1<<pg;
		m_disp[idx] = b;
	}
	return 0;
}


spi_transaction_t *SSD1309::getTransaction(pre_t pre)
{
	ssd1309_trans_t *t = 0;
	while (m_xtrans == 0xff) {
		spi_transaction_t *r;
		if (0 == spi_device_get_trans_result(m_hdl,&r,portMAX_DELAY)) {
			t = (ssd1309_trans_t *)r;
			int id = t - m_trans;
			assert((id >= 0) && (id < sizeof(m_trans)/sizeof(m_trans[0])));
			break;
		}
	}
	if (t == 0) {
		unsigned x = 0;
		while (m_xtrans & (1 << x))
			++x;
		m_xtrans |= (1<<x);
		assert(x < sizeof(m_trans)/sizeof(m_trans[0]));
		t = m_trans+x;
	}
	bzero(t,sizeof(ssd1309_trans_t));
	t->trans.user = t;
	t->gpio = m_dc;
	if (pre == pre_c) {
		t->set = true;
		t->lvl = false;
	} else if (pre == pre_d) {
		t->set = true;
		t->lvl = true;
	}
	return &t->trans;
}


int SSD1309::writeBytes(const uint8_t *data, unsigned len, pre_t pre)
{
	log_hex(TAG,data,len,"writeBytes %u",pre);
	spi_transaction_t *t = getTransaction(pre);
	if (len > sizeof(t->tx_data)) {
		t->tx_buffer = data;
	} else {
		memcpy(t->tx_data,data,len);
		t->flags = SPI_TRANS_USE_TXDATA;
	}
	t->length = len<<3;
	if (esp_err_t e = spi_device_queue_trans(m_hdl,t,portMAX_DELAY)) {
		log_warn(TAG,"error queuing read: %s",esp_err_to_name(e));
		return -1;
	}
	return 0;
}


int SSD1309::writeByte(uint8_t v, pre_t pre)
{
	return writeBytes(&v,1,pre);
}


int SSD1309::writeWord(uint8_t l, uint8_t h, pre_t pre)
{
	uint8_t d[] = {l,h};
	return writeBytes(d,sizeof(d),pre);
}


#endif
