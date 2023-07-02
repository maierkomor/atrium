/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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

#ifdef CONFIG_ILI9341

#include "ili9341.h"
#include "log.h"
#include "profiling.h"
#include "terminal.h"

#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG MODULE_ILI9341

#if IDF_VERSION >= 50
#define ets_delay_us esp_rom_delay_us
#endif

#if 1
#define log_devel log_dbug
#else
#define log_devel(...)
#endif

#define SPI_MAX_TX 4096

#define CHAR_WIDTH	6
#define CHAR_HEIGHT	8

#define CMD_NOP		0x00
#define CMD_RESET	0x01	// software reset
#define CMD_RDINFO	0x04	// read display identification information
#define CMD_RDDST	0x09	// read display status
#define CMD_RDDPM	0x0a	// read display power mode
#define CMD_RDMADCTL	0x0b	// read display madctl
#define CMD_RDDCOLMOD	0x0c	// read display pixel format
#define CMD_RDIMGFMT	0x0d	// read display image format
#define CMD_RDSIGMD	0x0e	// read display signal mode
#define CMD_RDDSDR	0x0f	// read BIST result
#define CMD_SLEEPIN	0x10	// enter sleep mode
#define CMD_SLEEPOUT	0x11	// exit sleep mode
#define CMD_PARTMDON	0x12	// partial mode on
#define CMD_NORMMDON	0x13	// normal mode on
#define CMD_INVOFF	0x20	// display inversion off
#define CMD_INVON	0x21	// display inversion on
#define CMD_GAMSET	0x26	// gamma set
#define CMD_DISPOFF	0x28	// display off
#define CMD_DISPON	0x29	// display on
#define CMD_CASET	0x2a	// column address set
#define CMD_PASET	0x2b	// page address set
#define CMD_RAMWR	0x2c	// memory write
#define CMD_COLORSET	0x2d	// color set
#define CMD_MEMRD	0x2e	// memory read
#define CMD_PARTAREA	0x30	// partial area
#define CMD_VSCRLDEF	0x33	// vertial scrolling definition
#define CMD_TEAROFF	0x34	// tearing effect line off
#define CMD_TEARON	0x35	// tearing effect line on
#define CMD_MADCTL	0x36	// memory access control
#define CMD_VSCRLST	0x37	// vertial scrolling start address
#define CMD_IDMOFF	0x38	// idle mode off
#define CMD_IDMON	0x39	// idle mode on
#define CMD_COLMOD	0x3a	// colmod: pixel format set
#define CMD_WRMEMC	0x3c	// write memory continue
#define CMD_RDMEMC	0x3e	// read memory continue
#define CMD_SETTEAR	0x44	// set tear scanline
#define CMD_GETSL	0x45	// get scanline
#define CMD_WRBRIGHT	0x51	// write brightness
#define CMD_RDBRIGHT	0x52	// read brightness
#define CMD_WRCTRLD	0x53	// write CTRL display
#define CMD_RDCTRLD	0x54	// read CTRL display
#define CMD_WRCADBR	0x55	// write content adaptive brightness control
#define CMD_RDCADBR	0x56	// read content adaptive brightness control
#define CMD_WRMINBR	0x5e	// write CABC minimum brightness
#define CMD_RDMINBR	0x5f	// read CABC minimum brightness
#define CMD_FRMCTR1	0x81	// frame control normal
#define CMD_ETMOD	0x87	// enter mode
#define CMD_DISCTRL	0xb6
#define CMD_PWCTRL1	0xc0
#define CMD_PWCTRL2	0xc1
#define CMD_PWCTRLA	0xcb
#define CMD_PWCTRLB	0xcf
#define CMD_VMCTRL1	0xc5
#define CMD_VMCTRL2	0xc7
#define CMD_RDID1	0xda	// read ID1
#define CMD_RDID2	0xdb	// read ID2
#define CMD_RDID3	0xdc	// read ID3
#define CMD_PGAMCTRL	0xe0	// positive gamma correction
#define CMD_NGAMCTRL	0xe1	// negative gamma correction
#define CMD_TIMCTRLA	0xe8
#define CMD_TIMCTRLB	0xea
#define CMD_PWRSEQ	0xed
#define CMD_IFCTL	0xf6

#define MADCTL_T2B	0x00
#define MADCTL_L2R	0x00
#define MADCTL_NRM	0x00
#define MADCTL_RGB	0x00
#define MADCTL_RL2R	0x00

#define MADCTL_B2T	0x80
#define MADCTL_R2L	0x40
#define MADCTL_REV	0x20
#define MADCTL_RR2L	0x04
#define MADCTL_MY	(1<<7)
#define MADCTL_MX	(1<<6)
#define MADCTL_MV	(1<<5)
#define MADCTL_ML	(1<<4)
#define MADCTL_BGR	(1<<3)
#define MADCTL_MH	(1<<2)


struct CmdName {
	uint8_t id;
	const char *name;
};

CmdName CmdNames[] = {
	{ CMD_NOP, "NOP" },
	{ CMD_RESET, "RESET" },
	{ CMD_RDINFO, "RDINFO" },
	{ CMD_RDDST, "RDDST" },
	{ CMD_RDDPM, "RDDPM" },
	{ CMD_RDMADCTL, "RDMADCTL" },
	{ CMD_RDDCOLMOD, "RDDCOLMOD" },
	{ CMD_RDIMGFMT, "RDIMGFMT" },
	{ CMD_RDSIGMD, "RDSIGMD" },
	{ CMD_RDDSDR, "RDDSDR" },
	{ CMD_SLEEPIN, "SLEEPIN" },
	{ CMD_SLEEPOUT, "SLEEPOUT" },
	{ CMD_PARTMDON, "PARTMDON" },
	{ CMD_NORMMDON, "NORMMDON" },
	{ CMD_INVOFF, "INVOFF" },
	{ CMD_INVON, "INVON" },
	{ CMD_GAMSET, "GAMSET" },
	{ CMD_DISPOFF, "DISPOFF" },
	{ CMD_DISPON, "DISPON" },
	{ CMD_CASET, "CASET" },
	{ CMD_PASET, "PASET" },
	{ CMD_RAMWR, "RAMWR" },
	{ CMD_COLORSET, "COLORSET" },
	{ CMD_MEMRD, "MEMRD" },
	{ CMD_PARTAREA, "PARTAREA" },
	{ CMD_VSCRLDEF, "VSCRLDEF" },
	{ CMD_TEAROFF, "TEAROFF" },
	{ CMD_TEARON, "TEARON" },
	{ CMD_MADCTL, "MADCTL" },
	{ CMD_VSCRLST, "VSCRLST" },
	{ CMD_IDMOFF, "IDMOFF" },
	{ CMD_IDMON, "IDMON" },
	{ CMD_COLMOD, "COLMOD" },
	{ CMD_WRMEMC, "WRMEMC" },
	{ CMD_RDMEMC, "RDMEMC" },
	{ CMD_SETTEAR, "SETTEAR" },
	{ CMD_GETSL, "GETSL" },
	{ CMD_WRBRIGHT, "WRBRIGHT" },
	{ CMD_RDBRIGHT, "RDBRIGHT" },
	{ CMD_WRCTRLD, "WRCTRLD" },
	{ CMD_RDCTRLD, "RDCTRLD" },
	{ CMD_WRCADBR, "WRCADBR" },
	{ CMD_RDCADBR, "RDCADBR" },
	{ CMD_WRMINBR, "WRMINBR" },
	{ CMD_RDMINBR, "RDMINBR" },
	{ CMD_FRMCTR1, "FRMCTR1" },
	{ CMD_ETMOD, "ETMOD" },
	{ CMD_DISCTRL, "DISCTRL" },
	{ CMD_PWCTRL1, "PWCTRL1" },
	{ CMD_PWCTRL2, "PWCTRL2" },
	{ CMD_PWCTRLA, "PWCTRLA" },
	{ CMD_PWCTRLB, "PWCTRLB" },
	{ CMD_VMCTRL1, "VMCTRL1" },
	{ CMD_VMCTRL2, "VMCTRL2" },
	{ CMD_RDID1, "RDID1" },
	{ CMD_RDID2, "RDID2" },
	{ CMD_RDID3, "RDID3" },
	{ CMD_PGAMCTRL, "PGAMCTRL" },
	{ CMD_NGAMCTRL, "NGAMCTRL" },
	{ CMD_TIMCTRLA, "TIMCTRLA" },
	{ CMD_TIMCTRLB, "TIMCTRLB" },
	{ CMD_PWRSEQ, "PWRSEQ" },
	{ CMD_IFCTL, "IFCTL" },

};

ILI9341 *ILI9341::Instance = 0;


ILI9341::ILI9341(uint8_t cs, uint8_t dc, int8_t reset, SemaphoreHandle_t sem, spi_device_handle_t hdl)
: MatrixDisplay(cs_bgr16)
, SpiDevice(drvName(), cs)
, m_hdl(hdl)
, m_sem(sem)
, m_dc((gpio_num_t)dc)
, m_reset((gpio_num_t)reset)
{
	writeCmd(CMD_RESET);
	vTaskDelay(10/portTICK_PERIOD_MS);
	uint8_t id[4];
	readRegs(CMD_RDINFO,id,sizeof(id));
	log_info(TAG,"display info %02x,%02x,%02x",id[1],id[2],id[3]);
	setOn(false);
	uint8_t a0xed[] = {0x55, 0x01, 0x23, 0x01};
	writeCmdArg(CMD_PWRSEQ,a0xed,sizeof(a0xed));
	uint8_t a0xe8[] = {0x85, 0x00, 0x78};
	writeCmdArg(CMD_TIMCTRLA,a0xe8,sizeof(a0xe8));
	uint8_t a0xea[] = {0x00, 0x00};
	writeCmdArg(CMD_TIMCTRLB,a0xea,sizeof(a0xea));
	writeCmdArg(CMD_PWCTRL1,0x21);
	writeCmdArg(CMD_PWCTRL2,0x10);
	uint8_t vcom[] = { 0x31, 0x3c };
	writeCmdArg(CMD_VMCTRL1,vcom,sizeof(vcom));
	writeCmdArg(CMD_VMCTRL2,0x86);
	writeCmdArg(CMD_COLMOD,0x5);
	writeCmd(CMD_INVOFF);
	writeCmd(CMD_IDMON);
	uint8_t colmod[2];
	readRegs(CMD_RDDCOLMOD,colmod,sizeof(colmod));
	log_info(TAG,"colmod 0x%02x",colmod[1]);
	sleepOut();
	setOn(true);
}


ILI9341::~ILI9341()
{
	free(m_temp);
	free(m_os);
}


void ILI9341::checkPowerMode()
{
	uint8_t dpm[3];
	readRegs(CMD_RDDPM,dpm,sizeof(dpm));
	log_info(TAG,"booster %s, idle %s, partial %s, sleep %s, normal %s, display %s"
		, dpm[2] & 0x80 ? "on" : "off"
		, dpm[2] & 0x40 ? "on" : "off"
		, dpm[2] & 0x20 ? "on" : "off"
		, dpm[2] & 0x10 ? "on" : "off"
		, dpm[2] & 0x8 ? "on" : "off"
		, dpm[2] & 0x4 ? "on" : "off"
	);
}


inline void ILI9341::setC()
{
	gpio_set_level(m_dc,0);
}


inline void ILI9341::setD()
{
	gpio_set_level(m_dc,1);
}


#ifdef CONFIG_IDF_TARGET_ESP8266
ILI9341 *ILI9341::create(spi_host_device_t host, int8_t cs, int8_t dc, int8_t reset)
#else
ILI9341 *ILI9341::create(spi_host_device_t host, spi_device_interface_config_t &cfg, int8_t dc, int8_t reset)
#endif
{
	assert(sizeof(wchar_t) == sizeof(uint16_t));	// needed for wmemset
	if (Instance) {
		log_warn(TAG,"instance already exists");
		return 0;
	}
	if ((dc < 0) || (reset < 0))
		return 0;
	log_dbug(TAG,"create with cs=%d, dc=%d, reset=%d",cfg.spics_io_num,dc,reset);
	if (esp_err_t e = gpio_set_direction((gpio_num_t)dc,GPIO_MODE_OUTPUT)) {
		log_warn(TAG,"cannot set gpio%d to output: %s",dc,esp_err_to_name(e));
		return 0;
	}
	gpio_set_level((gpio_num_t)dc,0);
	if (esp_err_t e = gpio_set_direction((gpio_num_t)cfg.spics_io_num,GPIO_MODE_OUTPUT)) {
		log_warn(TAG,"cannot set gpio%d to output: %s",cfg.spics_io_num,esp_err_to_name(e));
		return 0;
	}
	if (reset >= 0) {
		if (esp_err_t e = gpio_set_direction((gpio_num_t)reset,GPIO_MODE_OUTPUT)) {
			log_warn(TAG,"cannot set gpio%d to output: %s",reset,esp_err_to_name(e));
			return 0;
		}
		log_dbug(TAG,"reset triggered");
		gpio_set_level((gpio_num_t)reset,0);
		ets_delay_us(100);	// reset-pulse >= 10us
		gpio_set_level((gpio_num_t)reset,1);
		vTaskDelay(120);	// wait 120ms after reset
	}
	SemaphoreHandle_t sem = xSemaphoreCreateBinary();
	cfg.command_bits = 0;
	cfg.address_bits = 0;
	cfg.cs_ena_pretrans = 0;
	cfg.clock_speed_hz = SPI_MASTER_FREQ_8M;	// maximum: 10MHz
	cfg.queue_size = 8;
	cfg.post_cb = spidrv_post_cb_relsem;
	spi_device_handle_t hdl;
	if (esp_err_t e = spi_bus_add_device(host,&cfg,&hdl)) {
		log_warn(TAG,"device add failed: %s",esp_err_to_name(e));
		return 0;
	}
	Instance = new ILI9341(cfg.spics_io_num, dc, reset, sem, hdl);
	return Instance;
}


void ILI9341::reset()
{
	log_dbug(TAG,"reset triggered");
	gpio_set_level(m_reset,0);
	ets_delay_us(15);	// reset-pulse >= 10us
	gpio_set_level(m_reset,1);
	vTaskDelay(5);		// wait 5ms after reset
}


void ILI9341::sleepIn()
{
	if (writeCmd(CMD_SLEEPIN))
		log_warn(TAG,"sleep-in failed");
	vTaskDelay(120);	// wait 120ms to reach sleep
}


void ILI9341::sleepOut()
{
	log_dbug(TAG,"request sleep-out");
	writeCmd(CMD_SLEEPOUT);
	vTaskDelay(10);	// wait >= 5ms after sleep-out
}


int ILI9341::init(uint16_t maxx, uint16_t maxy, uint8_t options)
{
	log_info(TAG,"init(%u,%u)",maxx,maxy);
	if (maxx < maxy)
		writeCmdArg(CMD_MADCTL,0x48);
	else
		writeCmdArg(CMD_MADCTL,0x28);
	uint8_t madctl[2];
	readRegs(CMD_RDMADCTL,madctl,sizeof(madctl));
	log_info(TAG,"madctl: 0x%02x",madctl[1]);
	m_width = maxx;
	m_height = maxy;
	unsigned foss = maxx*maxy<<1;
	m_temp = (uint16_t *) heap_caps_malloc(SPI_MAX_TX, MALLOC_CAP_DMA);
	if (m_temp == 0) {
		log_error(TAG,"Out of memory.");
		return 1;
	}
	m_oss = 32 << 10;
	m_os = (uint16_t *) heap_caps_malloc(m_oss, MALLOC_CAP_DMA);
	if (m_os == 0) {
		m_oss = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
		m_os = (uint16_t *) heap_caps_malloc(m_oss, MALLOC_CAP_DMA);
		if (m_os == 0) {
			m_oss = 16 << 10;
			m_os = (uint16_t *) malloc(m_oss);
			if (m_os == 0) {
				m_oss = 8 << 10;
				m_os = (uint16_t *) malloc(m_oss);
				if (m_os == 0) {
					log_warn(TAG,"No memory for off-screen rendering.");
					m_oss = 0;
				}
			}
		}
	} else if (m_oss == foss) {
		log_dbug(TAG,"full off-screen buffer");
		m_fos = true;
		m_osx = 0;
		m_osy = 0;
		m_osw = maxx;
		m_osh = maxy;
	}
	log_dbug(TAG,"off-screen buffer: %u Bytes",m_oss);
//	checkPowerMode();
	writeCmd(CMD_IDMOFF);
	initOK();
	log_info(TAG,"ready");
	return 0;
}


int ILI9341::setOn(bool on)
{
	log_dbug(TAG,"setOn(%d)",on);
	return writeCmd(on ? CMD_DISPON : CMD_DISPOFF);
}

int ILI9341::setInvert(bool inv)
{
	log_dbug(TAG,"invert(%d)",inv);
	return writeCmd(inv ? CMD_INVON : CMD_INVOFF);
}


int ILI9341::setBrightness(uint8_t v)
{
	log_dbug(TAG,"setBrightness(%u)",v);
	return writeCmdArg(CMD_WRBRIGHT,v);
}


uint8_t ILI9341::fontHeight() const
{
	switch (m_font) {
	case -1: return 8;
	case -2: return 16;
	default:
		return Fonts[m_font].yAdvance;
	}
}

void ILI9341::flush()
{
	log_dbug(TAG,"flush %ux%u",m_width,m_height);
	PROFILE_FUNCTION();
	if (m_os)
		commitOffScreen();
}


/*
 * @param bg: -1: read from device, -2: do not init
 */
int ILI9341::setupOffScreen(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t bg)
{
	log_dbug(TAG,"setupOffScreen(%u,%u,%u,%u,%x)",x,y,w,h,bg);
	if ((x+w > m_width) || (y+h > m_height) || (w*h<<1 > m_oss))
		return -1;
	if (m_fos)
		return 0;
	m_osx = x;
	m_osy = y;
	m_osw = w;
	m_osh = h;
	if (bg >= 0) {
		wchar_t f = bg;
		wmemset((wchar_t*)m_os,f,w*h);
		if ((w*h) & 1)
			m_os[(w*h<<1)-1] = (uint16_t)(bg & 0xffff);
	} else {
		// read screen content
		uint8_t caset[] = { (uint8_t)(x>>8), (uint8_t)(x&0xff), (uint8_t)((x+w-1)>>8), (uint8_t)((x+w-1)&0xff) };
		writeCmdArg(CMD_CASET,caset,sizeof(caset));
		uint8_t paset[] = { (uint8_t)(y>>8), (uint8_t)(y&0xff), (uint8_t)((y+h-1)>>8), (uint8_t)((y+h-1)&0xff) };
		writeCmdArg(CMD_PASET,paset,sizeof(paset));
		writeCmd(CMD_MEMRD);
		readData((uint8_t*)m_os,w*h*2);
	}
	return 0;
}


void ILI9341::commitOffScreen()
{
	PROFILE_FUNCTION();
	log_dbug(TAG,"commitOffScreen() %u/%u->%u/%u",m_osx,m_osy,m_osx+m_osw,m_osy+m_osh);
	uint8_t caset[] = { (uint8_t)(m_osx>>8), (uint8_t)(m_osx&0xff), (uint8_t)((m_osx+m_osw-1)>>8), (uint8_t)((m_osx+m_osw-1)&0xff) };
	writeCmdArg(CMD_CASET,caset,sizeof(caset));
	uint8_t paset[] = { (uint8_t)(m_osy>>8), (uint8_t)(m_osy&0xff), (uint8_t)((m_osy+m_osh-1)>>8), (uint8_t)((m_osy+m_osh-1)&0xff) };
	writeCmdArg(CMD_PASET,paset,sizeof(paset));
	writeCmd(CMD_RAMWR);
	// TODO: full-off-screen is in non-DMA - i.e. could be one transaction
	uint32_t n = m_osw*m_osh<<1;
	uint32_t t = n > SPI_MAX_TX ? SPI_MAX_TX : n;
	writeData((uint8_t*)m_os,t);
	uint32_t off = 0;
	n -= t;
	while (n) {
		off += t;
		t = n > SPI_MAX_TX ? SPI_MAX_TX : n;
		writeCmd(CMD_WRMEMC);
		writeData((uint8_t*)m_os+off,t);
		n -= t;
	}
}


void ILI9341::drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg, int32_t bg)
{
	log_dbug(TAG,"drawBitmap(%u,%u,%u,%u,...,%x,%x)",x,y,w,h,fg,bg);
	if ((x >= m_width) || (y >= m_height))
		return;
	if ((x + w) > m_width)
		w = m_width - x;
	if ((y + h) > m_height)
		h = m_height - x;
	if ((w == 0) || (h == 0))
		return;
	uint16_t fgc = fg >= 0 ? (uint16_t)(fg & 0xffff) : m_colfg;
	unsigned len = w*h;
	unsigned idx = 0;
	uint8_t b = 0;
	if ((x >= m_osx) && (x+w < m_osx+m_osw) && (y >= m_osy) && (y < m_osy+m_osh)) {
		if (bg == -2) {
			while (idx != len) {
				if ((idx & 7) == 0)
					b = data[idx>>3];
				if (b&0x80)
					m_os[(x+idx%w-m_osx)+(y+idx/w-m_osy)*m_osw] = fgc;
				b<<=1;
				++idx;
			}
		} else {
			uint16_t bgc = bg >= 0 ? (uint16_t)(bg & 0xffff) : m_colbg;
			while (idx != len) {
				if ((idx & 7) == 0)
					b = data[idx>>3];
				m_os[(x+idx%w-m_osx)+(y+idx/w-m_osy)*m_osw] = b&0x80 ? fgc : bgc;
				b<<=1;
				++idx;
			}
		}
	} else if (bg == -2) {
		while (idx != len) {
			if ((idx & 7) == 0)
				b = data[idx>>3];
			if (b&0x80)
				setPixel(x+idx%w,y+idx/w,fgc);
			b<<=1;
			++idx;
		}
	} else {
		uint16_t bgc = bg >= 0 ? (uint16_t)(bg & 0xffff) : m_colbg;
		while (idx != len) {
			if ((idx & 7) == 0)
				b = data[idx>>3];
			setPixel(x+idx%w,y+idx/w,b&0x80?fgc:bgc);
			b<<=1;
			++idx;
		}
	}

}


void ILI9341::setPixel(uint16_t x, uint16_t y, int32_t col)
{
//	log_devel(TAG,"setPixel(%u,%u,%x) temp %u/%u+%u/%u",(unsigned)x,(unsigned)y,col,m_osx,m_osy,m_osw,m_osh);
	if ((x >= m_width) || (y >= m_height) || (col < 0) || (col > UINT16_MAX))
		return;
	uint16_t c = col >= 0 ? (uint16_t)(col & 0xffff) : m_colfg;
	if ((x >= m_osx) && (x < m_osx+m_osw) && (y >= m_osy) && (y < m_osy+m_osh)) {
		m_os[(x-m_osx)+(y-m_osy)*m_osw] = c;
	} else {
		log_devel(TAG,"setPixel(%u,%u,%x) offscreen temp %u/%u+%u/%u",(unsigned)x,(unsigned)y,col,m_osx,m_osy,m_osw,m_osh);
		uint8_t caset[] = { (uint8_t)(x>>8), (uint8_t)(x&0xff), (uint8_t)((x+1)>>8), (uint8_t)((x+1)&0xff) };
		writeCmdArg(CMD_CASET,caset,sizeof(caset));
		uint8_t paset[] = { (uint8_t)(y>>8), (uint8_t)(y&0xff), (uint8_t)((y+1)>>8), (uint8_t)((y+1)&0xff) };
		writeCmdArg(CMD_PASET,paset,sizeof(paset));
		writeCmd(CMD_RAMWR);
		writeData((uint8_t*)&c,2);
	}
}


void ILI9341::drawHLine(uint16_t x, uint16_t y, uint16_t n, int32_t col)
{
	log_dbug(TAG,"drawHLine(%d,%d,%u,%x)",x,y,n,col);
	if (x >= m_width)
		return;
	if ((x + n) > m_width)
		n = m_width-x;
	uint16_t c = col >= 0 ? (uint16_t) (col & 0xffff) : m_colfg;
	fillRect(x,y,n,1,c);
}


void ILI9341::drawVLine(uint16_t x, uint16_t y, uint16_t n, int32_t col)
{
	log_dbug(TAG,"drawVLine(%d,%d,%u,%x)",x,y,n,col);
	if (y >= m_height)
		return;
	if ((y + n) > m_height)
		n = m_height - y;
	uint16_t c = col >= 0 ? (uint16_t) (col & 0xffff) : m_colfg;
	fillRect(x,y,1,n,c);
}


int32_t ILI9341::getColor(color_t c) const
{
	switch (c) {
	case WHITE:	return 0xffff;
	case BLACK:	return 0x0000;
	case BLUE:	return 0xfc00;
	case RED:	return 0x03c0;
	case GREEN:	return 0x007f;
//	case PURPLE:	return 0xffc0;
	case YELLOW:	return 0x03ff;
	case CYAN:	return 0xfc7f;
	case MAGENTA:	return 0x0fc0;
	default:	return -1;
	}
}


void ILI9341::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t c)
{
	PROFILE_FUNCTION();
	log_dbug(TAG,"fillRect(%d,%d,%d,%d,%x)",x,y,w,h,c);
	uint16_t col = c == -1 ? m_colfg : (uint16_t)(c&0xffff);
	if ((x >= m_osx) && (y >= m_osy) && (x+w < m_osx+m_osw) && (y+w < m_osy+m_osh)) {
		for (uint16_t j = y; j < y+h; ++j) {
			for (uint16_t i = x; i < x+w; ++i)
				m_os[(j-m_osy) * m_osw + i - m_osx] = col;
		}
	} else {
		uint8_t caset[] = { (uint8_t)(x>>8), (uint8_t)(x&0xff), (uint8_t)((x+w-1)>>8), (uint8_t)((x+w-1)&0xff) };
		writeCmdArg(CMD_CASET,caset,sizeof(caset));
		uint8_t paset[] = { (uint8_t)(y>>8), (uint8_t)(y&0xff), (uint8_t)((y+h-1)>>8), (uint8_t)((y+h-1)&0xff) };
		writeCmdArg(CMD_PASET,paset,sizeof(paset));
		writeCmd(CMD_RAMWR);
		unsigned n = w*h;
		unsigned b = (n*2 > SPI_MAX_TX) ? (SPI_MAX_TX>>1) : n;
		log_dbug(TAG,"n=%u, b=%u",n,b);
		wmemset((wchar_t*)m_temp,col,b);
		setD();
		do {
			unsigned s = ((n<<1) > SPI_MAX_TX) ? SPI_MAX_TX : n<<1;
			writeBytes((uint8_t*)m_temp,s);
			n -= (s >> 1);
		} while (n);
	}
}


int ILI9341::readRegs(uint8_t reg, uint8_t *data, uint8_t num)
{
	if (Modules[TAG]) {
		const char *cmd = "???";
		for (auto &c : CmdNames) {
			if (c.id == reg) {
				cmd = c.name;
				break;
			}
		}
		log_dbug(TAG,"0x%02x/CMD_%s",reg,cmd);
	}
	assert(num > 1);
	bzero(data,num);
	setC();
	spi_transaction_t *t = getTransaction();
	bzero(t,sizeof(*t));
	t->user = m_sem;
	if (num <= 4) {
		t->tx_data[0] = reg;
		t->flags = SPI_TRANS_USE_RXDATA|SPI_TRANS_USE_TXDATA;
	} else {
		uint8_t *txbuf = (uint8_t*)alloca(num);
		bzero(txbuf,num);
		txbuf[0] = reg;
		t->tx_buffer = txbuf;
	}
	t->rx_buffer = data;
	t->length = num << 3;
	t->rxlength = num << 3;
	if (esp_err_t e = spi_device_queue_trans(m_hdl,t,portMAX_DELAY)) {
		log_warn(TAG,"error queuing read: %s",esp_err_to_name(e));
		return -1;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"ili9341");
	if (num <= 4)
		memcpy(data,t->rx_data,num);
	log_hex(TAG,data,num,"read regs %u@0x%02x:",num,reg);
	return 0;
}


/*
 * The text is assumed to fit into the off-screen rendering buffer.
 * No control-chars may be handed to this function.
 */
unsigned ILI9341::drawChars(const char *at, const char *e, int32_t fg, int32_t bg)
{
	log_devel(TAG,"drawChars(%s,%d,%x,%x)",at,e-at,fg,bg);
	uint16_t x = m_osx;
	while (at != e) {
		x += drawChar(x,m_osy,*at,fg,bg);
		++at;
	}
	return x;
}

unsigned ILI9341::drawText(uint16_t x, uint16_t y, const char *txt, int n, int32_t fg, int32_t bg)
{
	log_dbug(TAG,"drawText(%d,%d,'%s')",x,y,txt);
	if (n < 0)
		n = strlen(txt);
	const Font *font = Fonts+(int)m_font;
	uint16_t h= font->yAdvance;
	uint16_t a = 0;
	const char *at = txt, *e = txt + n;
	while (at != e) {
		char c = *at;
		if (c == '\n') {
			x = 0;
			y += h;
			++at;
		}
		if (c == 0)
			return a;
		uint16_t da = 0;
		const char *st = at;
		while (at != e) {
			c = *at;
			if (c == 0)
				break;
			if (c == '\n')
				break;
			uint16_t cw = charWidth(c);
			if (m_posx + cw > m_width)	// TODO fix
				return a;
			if (((da + cw) * h<<1) > m_oss)
				break;
			da += cw;
			++at;
		}
		if ((da * h << 1 > m_oss) || (st == at)) {
			// too big for off-screen rendering
			break;
		}
		if (!m_fos)
			setupOffScreen(x+a,y,da,h,bg == -1 ? m_colbg : bg);
		drawChars(st,at,fg,bg);
		a += da;
		if (!m_fos)
			commitOffScreen();
	}
	if (at != e)
		log_dbug(TAG,"drawText on screen");
	while (at != e) {
		if (*at == '\n') {
			x = 0;
			y += fontHeight();
			++at;
		}
		if (*at == 0)
			break;
		x += drawChar(x,y,*at,fg,bg);
		++at;
	}
	return a;
#if 0
	uint16_t width = 0;
	const char *e = txt + n;
	const char *at = txt;
	while (at != e) {
		char c = *at++;
		if ((c < font->first) || (c > font->last))
			continue;
		if (c == '\n') {
			height += font->yAdvance;
		} else {
			uint8_t ch = c - font->first;
			if (x + font->glyph[ch].xAdvance > m_width)
				break;
			width += font->glyph[ch].xAdvance;
		}
	}
	e = at;
	at = txt;
	while (at != e) {
		// TODO
		uint8_t c = *at++;
		const uint8_t *data = font->bitmap + font->glyph[c].bitmapOffset;
	}
	return a;
#endif
}


int ILI9341::writeCmd(uint8_t v)
{
	if (Modules[TAG]) {
		const char *cmd = "???";
		for (auto &c : CmdNames) {
			if (c.id == v) {
				cmd = c.name;
				break;
			}
		}
		log_devel(TAG,"writeCmd 0x%02x/CMD_%s",v,cmd);
	}
#if 0
	setC();
	spi_transaction_t t;
	bzero(&t,sizeof(t));
	t.user = m_sem;
	t.cmd = v;
	if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,1)) {
		log_warn(TAG,"error queuing writeCmd: %s",esp_err_to_name(e));
		return -1;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"ili9341");
#else
	setC();
	writeByte(v);
#endif
	return 0;
}


int ILI9341::writeCmdArg(uint8_t v, uint8_t a)
{
	if (Modules[TAG]) {
		const char *cmd = "???";
		for (auto &c : CmdNames) {
			if (c.id == v) {
				cmd = c.name;
				break;
			}
		}
		log_dbug(TAG,"writeCmdArg 0x%02x/CMD_%s 0x%02x",v,cmd,a);
	}
#if 0
	setC();
	spi_transaction_t t;
	bzero(&t,sizeof(t));
	t.user = m_sem;
	t.cmd = v;
	t.length = 8;
	t.tx_data[0] = v;
	t.flags = SPI_TRANS_USE_TXDATA;
	if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,1)) {
		log_warn(TAG,"error queuing writeCmdArg: %s",esp_err_to_name(e));
		return -1;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"ili9341");
#else
	setC();
	writeByte(v);
	setD();
	writeByte(a);
#endif
	return 0;
}


int ILI9341::writeCmdArg(uint8_t v, uint8_t *a, size_t n)
{
	if (Modules[TAG]) {
		const char *cmd = "???";
		for (auto &c : CmdNames) {
			if (c.id == v) {
				cmd = c.name;
				break;
			}
		}
		log_hex(TAG,a,n,"writeCmdArg 0x%02x/CMD_%s",v,cmd);
	}
#if 0
	setC();
	spi_transaction_t t;
	bzero(&t,sizeof(t));
	t.user = m_sem;
	t.cmd = v;
	t.length = n << 3;
	if (n <= 4) {
		t.flags = SPI_TRANS_USE_TXDATA;
		memcpy(t.tx_data,a,n);
	} else {
		t.tx_buffer = a;
	}
	if (esp_err_t e = spi_device_queue_trans(m_hdl,&t,1)) {
		log_warn(TAG,"error queuing writeCmdArg: %s",esp_err_to_name(e));
		return -1;
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_sem,"ili9341");
#else
	setC();
	writeByte(v);
	setD();
	writeBytes(a,n);
#endif
	return 0;
}


int ILI9341::writeData(uint8_t v)
{
	setD();
	return writeByte(v);
}


int ILI9341::writeData(uint8_t *v, unsigned len)
{
	setD();
	return writeBytes(v,len);
}


int ILI9341::readData(uint8_t *v, unsigned len)
{
	setD();
	return readBytes(v,len);
}


int ILI9341::readBytes(uint8_t *data, unsigned len)
{
	// TODO test
	log_devel(TAG,"readBytes %p: %u",data,len);
	assert(data);
	esp_err_t e = 0;
	size_t off = 0;
	spi_device_acquire_bus(m_hdl,portMAX_DELAY);
	while (len) {
		spi_transaction_t *t = getTransaction();
		bzero(t,sizeof(*t));
		t->tx_buffer = data + off;
		t->rx_buffer = data + off;
		if (len > SPI_MAX_TX) {
			t->length = SPI_MAX_TX<<3;
			t->rxlength = SPI_MAX_TX<<3;
			t->flags = SPI_TRANS_CS_KEEP_ACTIVE;
			len -= SPI_MAX_TX;
			off += SPI_MAX_TX;
		} else {
			t->length = len<<3;
			t->rxlength = len<<3;
			len = 0;
			t->user = m_sem;
		}
		log_devel(TAG,"readBytes %d@%p",t->length>>3,t->tx_buffer);
		e = spi_device_queue_trans(m_hdl,t,portMAX_DELAY);
		if (e) {
			log_warn(TAG,"error queuing readBytes: %s",esp_err_to_name(e));
			return -1;
		}
	}
	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT*4))
		abort_on_mutex(m_sem,"ili9341");
	return 0;

}


int ILI9341::writeByte(uint8_t v)
{
	spi_transaction_t *t = getTransaction();
	bzero(t,sizeof(*t));
//	t->user = m_sem;
	t->cmd = v;
	t->length = 8;
	t->flags = SPI_TRANS_USE_TXDATA;
	t->tx_data[0] = v;
	if (esp_err_t e = spi_device_queue_trans(m_hdl,t,portMAX_DELAY)) {
		log_warn(TAG,"error queuing writeByte: %s",esp_err_to_name(e));
		return -1;
	}
//	if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT))
//		abort_on_mutex(m_sem,"ili9341");
//	log_dbug(TAG,"writeB 0x%02x",v);
	return 0;
}


spi_transaction_t *ILI9341::getTransaction()
{
	while (m_xtrans == 0xff) {
		spi_transaction_t *r;
		esp_err_t x = spi_device_get_trans_result(m_hdl,&r,portMAX_DELAY);
		if (x == 0) {
			int id = r - m_trans;
			if ((id >= 0) && (id < 8))
				m_xtrans &= ~(1<<id);
		}
	}
	unsigned x = 0;
	while (m_xtrans & (1 << x))
		++x;
	m_xtrans |= (1<<x);
	assert(x < 8);
	return m_trans+x;
}


int ILI9341::writeBytes(uint8_t *data, unsigned len)
{
	PROFILE_FUNCTION();
	log_devel(TAG,"writeBytes %u@%p",len,data);
	assert(data);
	esp_err_t e = 0;
	uint8_t *end = data + len;
	while (data != end) {
		spi_transaction_t *t = getTransaction();
		bzero(t,sizeof(spi_transaction_t));
		t->rxlength = 8;
		t->flags = SPI_TRANS_USE_RXDATA;
		t->tx_buffer = data;
		if (end-data > SPI_MAX_TX) {
			t->length = SPI_MAX_TX<<3;
			data += SPI_MAX_TX;
		} else {
			t->length = len<<3;
			t->user = m_sem;
			len = 0;
			data = end;
		}
//		log_devel(TAG,"writeBytes %d@%p",t.length>>3,t.tx_buffer);
		e = spi_device_queue_trans(m_hdl,t,portMAX_DELAY);
		if (e)
			break;
	}
	if (e == 0) {
		if (pdTRUE != xSemaphoreTake(m_sem,MUTEX_ABORT_TIMEOUT*4))
			abort_on_mutex(m_sem,"ili9341");
	} else {
		log_warn(TAG,"error queuing writeBytes: %s",esp_err_to_name(e));
	}
	return 0;
}


const char *ILI9341::exeCmd(struct Terminal &t, int argc, const char **argv)
{
	if (argc == 0)
		return 0;
	unsigned arg2 = 0x100;
	if (argc >= 2) {
		char *e;
		long l = strtol(argv[1],&e,0);
		if ((*e == 0) && (l >= 0) && (l <= UINT8_MAX))
			arg2 = l;
	}
	if (!strcmp(argv[0],"info")) {
		uint8_t v[3];
		readRegs(CMD_RDINFO,v,sizeof(v));
	} else if (!strcmp(argv[0],"status")) {
		uint8_t v[5];
		readRegs(CMD_RDDST,v,sizeof(v));
		t.printf("booster is %s\n",v[2]&128?"on":"off");
		t.printf("row is %s\n",v[2]&64?"bottom-to-top":"top-to-bottom");
		t.printf("column is %s\n",v[2]&32?"right-to-left":"left-to-right");
		t.printf("reverse is %s\n",v[2]&16?"on":"off");
		t.printf("refresh is %s\n",v[2]&8?"bottom-to-top":"top-to-bottom");
		t.printf("refresh is %s\n",v[2]&2?"right-to-left":"left-to-right");
		t.printf("color is %s\n",v[2]&4?"BGR":"RGB");
	} else if (!strcmp(argv[0],"bright")) {
		if ((argc == 1) || (arg2 == 0x100)) {
			uint8_t v[2];
			readRegs(CMD_RDBRIGHT,v,sizeof(v));
			t.printf("brightness %u\n",v[1]);
		} else {
			writeCmdArg(CMD_WRBRIGHT,arg2);
//			t.printf("brightness %u",arg2);
		}
	} else if (!strcmp(argv[0],"mode")) {
		uint8_t v[2];
		readRegs(CMD_RDDPM,v,sizeof(v));
		t.printf("display is %s\n",v[1]&4?"on":"off");
		t.printf("sleep is %s\n",v[1]&16?"on":"off");
		t.printf("idle is %s\n",v[1]&64?"on":"off");
		t.printf("booster is %s\n",v[1]&128?"on":"off");
	} else if (!strcmp(argv[0],"madctl")) {
		if ((argc == 1) || (arg2 == 0x100)) {
			uint8_t v[2];
			readRegs(CMD_RDMADCTL,v,sizeof(v));
			t.printf("madctl=0x%x,%x,%x\n",v[0],v[1]);
			t.printf("%s, %s\n",v[1]&128?"bottom-to-top":"top-to-bottom",v[1]&64?"right-to-left":"left-to-right");
			t.printf("reverse is %s\n",v[1]&32?"on":"off");
			t.printf("refresh is %s\n",v[1]&16?"bottom-to-top":"top-to-bottom");
			t.printf("color is %s\n",v[1]&8?"BGR":"RGB");
			t.printf("refresh is %s\n",v[1]&4?"right-to-left":"left-to-right");
		} else {
			writeCmdArg(CMD_MADCTL,arg2);
		}
	} else if (!strcmp(argv[0],"colmod")) {
		if ((argc == 1) || (arg2 == 0x100)) {
			uint8_t colmod[2];
			readRegs(CMD_RDDCOLMOD,colmod,sizeof(colmod));
			log_info(TAG,"colmod 0x%02x",colmod[1]);
		} else {
			writeCmdArg(CMD_COLMOD,arg2);
		}
	} else {
		char *e;
		long l = strtol(argv[0],&e,0);
		uint8_t arg1 = 0;
		if ((*e == 0) && (l >= 0) && (l <= UINT8_MAX)) {
			arg1 = (uint8_t) l;
		} else {
			arg1 = 0;
			for (const auto &c : CmdNames) {
				if (0 == strcasecmp(c.name,argv[0])) {
					arg1 = c.id;
					break;
				}
			}
		}
		if (arg1) {
			if (arg2 == 0x100)
				writeCmd(arg1);
			else
				writeCmdArg(arg1,arg2);
		} else {
			return "Invalid argument #1.";
		}
	}
	return 0;
	}

#endif

