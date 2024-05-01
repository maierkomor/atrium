/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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

#ifdef CONFIG_SSD1306

#include "ssd1306.h"
#include "log.h"
#include "profiling.h"

#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define CHAR_WIDTH	6
#define CHAR_HEIGHT	8

#define CTRL_CMD1	0x00	// command and data
#define CTRL_CMDN	0x80	// command with more commands
#define CTRL_CMDC	0xc0	// continuation command
#define CTRL_DATA	0x00	// data only

#define SSD1306_ADDR0	0x3c
#define SSD1306_ADDR1	0x3d

#define CMD_NOP		0xe3

#define TAG MODULE_SSD130X



SSD1306 *SSD1306::Instance = 0;


SSD1306::SSD1306(uint8_t bus, uint8_t addr)
: I2CDevice(bus,addr,drvName())
{
	Instance = this;
	log_info(TAG,"ssd1306 at %u,0x%x",bus,addr);
}


int SSD1306::init(uint16_t maxx, uint16_t maxy, uint8_t hwcfg)
{
	log_info(TAG,"init(%u,%u)",maxx,maxy);
	m_width = maxx;
	m_height = maxy;
	uint32_t dsize = maxx * maxy;
	m_disp = (uint8_t *) malloc(dsize); // two dimensional array of n pages each of n columns.
	if (m_disp == 0) {
		log_error(TAG,"out of memory");
		return 1;
	}
	uint8_t setup[] = {
		m_addr,
		0x00,				// command
		0xae,				// display off
		0xd5, 0x80,			// oszi freq (default), clock div=1 (optional)
		0xa8, (uint8_t)(m_height-1),	// MUX
		0xd3, 0x00,			// display offset (optional)
		0x40,				// display start line	(optional)
		0x8d, 0x14,			// enable charge pump
		0x20, 0x00,			// address mode: horizontal
		0xa0,				// map address 0 to seg0
		(uint8_t) (0xc0 | (hwcfg&hwc_iscan)),	// scan 0..n
		0xda,				// COM hardware config
		(uint8_t) (hwcfg&(hwc_rlmap|hwc_altm)),	
		0x81, 0x80,			// medium contrast
		0xd9, 0x22,			// default pre-charge (optional)
		0xa4,				// output RAM
		0xa6,				// normal mode, a7=inverse
		0x2e,				// no scrolling
	};
	if (i2c_write(m_bus,setup,sizeof(setup),1,1))
		return 1;
	clear();
	flush();
	setOn(true);
	initOK();
	log_info(TAG,"ready");
	return 0;
}

int SSD1306::setOn(bool on)
{
	log_dbug(TAG,"setOn(%d)",on);
	uint8_t cmd_on[] = { m_addr, 0x00, 0x8d, 0x1f, 0xaf };
	uint8_t cmd_off[] = { m_addr, 0x00, 0xae, 0x8d, 0x10 };
	return i2c_write(m_bus,on ? cmd_on : cmd_off,sizeof(cmd_on),1,1);
}

int SSD1306::setInvert(bool inv)
{
	log_dbug(TAG,"invert(%d)",inv);
	uint8_t cmd[3] = { m_addr, 0x00, 0xa6 };
	if (inv)
		cmd[2] |= 1;
	return i2c_write(m_bus,cmd,sizeof(cmd),1,1);
}


int SSD1306::setBrightness(uint8_t contrast)
{
	uint8_t cmd[] = { m_addr, 0x00, 0x81, contrast };
	return i2c_write(m_bus,cmd,sizeof(cmd),1,1);
}


void SSD1306::flush()
{
	if (m_dirty == 0)
		return;
	PROFILE_FUNCTION();
	uint8_t cmd[] = { m_addr, 0x00, 0xb0, 0x21, 0x00, (uint8_t)(m_width-1) };
	uint8_t pfx[] = { m_addr, 0x40 };
	uint8_t numpg = m_height / 8 + ((m_height & 7) != 0);
	unsigned pgs = m_width;
	if (pgs == 128) {
		if (m_dirty == 0xff) {
			i2c_write(m_bus,cmd,sizeof(cmd),1,1);
			i2c_write(m_bus,pfx,sizeof(pfx),0,1);
			i2c_write(m_bus,m_disp,128*8,1,0);
			log_dbug(TAG,"sync 0-7");
			m_dirty = 0;
		} else if (m_dirty == 0xf) {
			i2c_write(m_bus,cmd,sizeof(cmd),1,1);
			i2c_write(m_bus,pfx,sizeof(pfx),0,1);
			i2c_write(m_bus,m_disp,128*4,1,0);
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
				i2c_write(m_bus,cmd,sizeof(cmd),1,1);
				i2c_write(m_bus,pfx,sizeof(pfx),0,1);
				i2c_write(m_bus,m_disp+f*pgs,n*pgs,1,0);
				log_dbug(TAG,"sync %u-%u",f,p);
			}
			++p;
		}
		m_dirty = 0;
	}
}


SSD1306 *SSD1306::create(uint8_t bus, uint8_t addr)
{
	uint8_t cmd[] = { (uint8_t)addr, CMD_NOP };
	if (0 == i2c_write(bus,cmd,sizeof(cmd),1,1)) {
		return new SSD1306(bus,addr);
	}
	return 0;
}


static int ssd1306_test(uint8_t bus, uint8_t addr)
{
	if (0 == i2c_write1(bus,addr,CMD_NOP)) {
		new SSD1306(bus,addr);
		return 1;
	} else {
		log_dbug(TAG,"no SSD1306 at 0x%x",addr);
	}
	return 0;
}


unsigned ssd1306_scan(uint8_t bus)
{
	log_dbug(TAG,"searching for SSD1306");
	uint8_t addrs[] = { 0x3c<<1, 0x3d<<1 };
	for (uint8_t addr : addrs) {
		if (ssd1306_test(bus,addr<<1))
			return 1;
	}
	return 0;
}

#endif
