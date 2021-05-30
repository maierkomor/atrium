/*
 *  Copyright (C) 2021, Thomas Maier-Komor
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

#if defined CONFIG_I2C && defined CONFIG_LEDDISP

#include "ht16k33.h"
#include "i2cdrv.h"
#include "log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <strings.h>


#define CMD_SYSTEM	0x20
#define CMD_SYS_OFF	0x20
#define CMD_SYS_ON	0x21
#define CMD_WDATA	0x00

#define CMD_DISP	0x80
#define CMD_DISP_ON	0x81
#define CMD_DISP_2HZ	0x83
#define CMD_DISP_1HZ	0x85
#define CMD_DISP_05HZ	0x87

#define CMD_DIM		0xe0

#define DEV_ADDR_MIN	(0x70 << 1)
#define DEV_ADDR_MAX	(0x77 << 1)

static const char TAG[] = "ht16k33";


typedef enum { disp_off, disp_on, blink_05hz, blink_1hz, blink_2hz } disp_t;


unsigned HT16K33::create(uint8_t bus)
{
	unsigned r = 0;
	for (uint8_t a = DEV_ADDR_MIN; a < DEV_ADDR_MAX; a += 2) {
		uint8_t cmd[] = { a , CMD_SYS_ON };
		if (i2c_write(bus,cmd,sizeof(cmd),true,true))
			continue;
		log_dbug(TAG,"found");
		HT16K33 *dev = new HT16K33(bus,a);
		dev->init();
		++r;
	}
	return r;
}


int HT16K33::init()
{
	setOn(true);
	setBlink(0);
	setDim(0xf);
	return 0;
}


const char *HT16K33::drvName() const
{
	return "ht16k33";
}


int HT16K33::setOffset(unsigned pos)
{
	log_dbug(TAG,"setOff(%u)",pos);
	if (pos > 15)
		return 1;;
	m_pos = pos;
	return 0;
}


void HT16K33::clear()
{
	uint8_t data[18];
	bzero(data,sizeof(data));
	data[0] = m_addr;
	i2c_write(m_bus,data,sizeof(data),true,true);
	m_pos = 0;
}


int HT16K33::write(uint8_t v, int off)
{
	log_dbug(TAG,"write(%x,%d)",v,off);
	if (off < 0)
		off = m_pos;
	uint8_t data[] = { m_addr , (uint8_t)(off&0xf), v };
	if (i2c_write(m_bus,data,sizeof(data),true,true))
		return -1;
	m_pos = (off + 1) & 0xf;
	return 0;
}


int HT16K33::write(uint8_t *v, unsigned n, int off)
{
	log_dbug(TAG,"write(%x...,%d)",*v,off);
	if (off < 0)
		off = m_pos;
	uint8_t data[] = { m_addr , (uint8_t)(off&0xf) };
	if (i2c_write(m_bus,data,sizeof(data),false,true))
		return -1;
	if (i2c_write(m_bus,v,n,true,false))
		return -1;
	m_pos = (off + n) & 0xf;
	return 0;
}


int HT16K33::setOn(bool on)
{
	uint8_t cmd[] = { m_addr, (uint8_t)(CMD_SYSTEM | on) };
	return i2c_write(m_bus,cmd,sizeof(cmd),true,true);
}


int HT16K33::setBlink(uint8_t blink)
{
	uint8_t cmd[] = { m_addr, (uint8_t)(CMD_DISP_ON | 0) };
	return i2c_write(m_bus,cmd,sizeof(cmd),true,true);
}


uint8_t HT16K33::getDim() const
{
	return m_dim;
}


int HT16K33::setDim(uint8_t dim)
{
	if (dim > 15)
		return 1;
	uint8_t cmd[] = { m_addr, (uint8_t)(CMD_DIM | dim) };
	if (i2c_write(m_bus,cmd,sizeof(cmd),true,true))
		return -1;
	m_dim = dim;
	return 0;
}


int HT16K33::setNumDigits(unsigned n)
{
	if (n > 16)
		return -1;
	return 0;
}


unsigned ht16k33_scan(uint8_t bus)
{
	return HT16K33::create(bus) != 0;
}

#endif
