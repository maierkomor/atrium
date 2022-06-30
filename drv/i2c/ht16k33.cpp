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

#if defined CONFIG_HT16K33

#include "ht16k33.h"
#include "i2cdrv.h"
#include "log.h"

#include <string.h>


#define CMD_SYSTEM	0x20
#define CMD_SYS_OFF	0x20
#define CMD_SYS_ON	0x21
#define CMD_WDATA	0x00

#define CMD_DISP	0x80
#define CMD_DISP_ON	0x81
#define CMD_DISP_2HZ	0x83
#define CMD_DISP_1HZ	0x85
#define CMD_DISP_05HZ	0x87
#define CMD_RDINTR	0x60

#define CMD_DIM		0xe0

#define DEV_ADDR_MIN	0x70
#define DEV_ADDR_MAX	0x77

#define TAG MODULE_HT16K33


typedef enum { disp_off, disp_on, blink_05hz, blink_1hz, blink_2hz } disp_t;


void HT16K33::create(uint8_t bus, uint8_t addr)
{
	if (addr == 0) {
		for (uint8_t a = DEV_ADDR_MIN; a <= DEV_ADDR_MAX; ++a)
			create(bus,a);
	} else if ((addr < DEV_ADDR_MIN) || (addr > DEV_ADDR_MAX)) {
		log_warn(TAG,"invalid address 0x%x",(unsigned)addr);
	} else {
		addr <<= 1;
		if (i2c_write1(bus,addr,CMD_SYS_ON)) {
			log_warn(TAG,"no HT16k33 at %u,0x%x",bus,(unsigned)addr);
		} else {
			log_info(TAG,"found at 0x%x",(unsigned)addr>>1);
			HT16K33 *dev = new HT16K33(bus,addr);
			dev->init();
		}
	}
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
	if (pos >= m_digits)
		return 1;;
	m_pos = pos;
	return 0;
}


int HT16K33::clear()
{
	uint8_t data[18] = { 0 };
	data[0] = m_addr;
	i2c_write(m_bus,data,sizeof(data),true,true);
	m_pos = 0;
	bzero(m_data,sizeof(m_data));
	return 0;
}


int HT16K33::setPos(uint8_t x, uint8_t y)
{
	m_pos = x;
	if ((x >= m_digits) || (y != 0))
		return 1;
	return 0;
}


int HT16K33::write(uint8_t v)
{
	log_dbug(TAG,"write(%x)",v);
	if (m_pos >= m_digits)
		return 1;
	uint8_t off = m_pos;
	if (m_data[off] != v) {
		uint8_t data[] = { m_addr, (uint8_t)off, v };
		if (i2c_write(m_bus,data,sizeof(data),true,true))
			return -1;
		m_data[off] = v;
	}
	++off;
	m_pos = off;
	return 0;
}


int HT16K33::write(uint8_t *v, unsigned n)
{
	log_dbug(TAG,"write(%x...)",*v);
	if (m_pos >= m_digits)
		return 1;
	uint8_t off = m_pos;
	if (n + off >= m_digits)
		n = m_digits - off;
	if (0 != memcmp(m_data+off,v,n)) {
		uint8_t data[] = { m_addr , (uint8_t)off };
		if (i2c_write(m_bus,data,sizeof(data),false,true))
			return -1;
		if (i2c_write(m_bus,v,n,true,false))
			return -1;
		memcpy(m_data+off,v,n);
	}
	off += n;
	m_pos = off;
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


int HT16K33::setNumDigits(unsigned n)
{
	if (n <= sizeof(m_data)) {
		uint8_t cmd[] = { m_addr, (uint8_t)(CMD_DIM | (n-1)) };
		if (0 == i2c_write(m_bus,cmd,sizeof(cmd),true,true)) {
			m_digits = n;
			return 0;
		}
	}
	return -1;
}


#endif
