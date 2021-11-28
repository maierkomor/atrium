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

#if defined CONFIG_PCF8574

#include "log.h"
#include "pcf8574.h"

#include <string.h>


#define DEV_ADDR_MIN	(0x20 << 1)
#define DEV_ADDR_MAX	(0x28 << 1)	// out of range

#define TAG MODULE_PCF8574

PCF8574 *PCF8574::Instance = 0;

// 8 GPIOs
// set to hi for use as input!

unsigned PCF8574::create(uint8_t bus)
{
	unsigned r = 0;
	for (uint8_t a = DEV_ADDR_MIN; a < DEV_ADDR_MAX; a += 2) {
		uint8_t data;
		if (i2c_read(bus,a,&data,sizeof(data)))
			continue;
		log_dbug(TAG,"found");
		PCF8574 *dev = new PCF8574(bus,a);
		if (Instance) {
			log_warn(TAG,"multiple devices");
		} else {
			Instance = dev;
		}
		++r;
	}
	return r;
}


const char *PCF8574::drvName() const
{
	return "pcf8574";
}


int PCF8574::init()
{
	return 0;
}


int PCF8574::setGpio(bool value, unsigned off)
{
	if (off >= 8)
		return -1;
	if (((m_data >> off) & 1) != value) {
		uint8_t d = m_data;
		if (value)
			d |= (1<<off);
		else
			d &= ~(1<<off);
		if (i2c_write1(m_bus,m_addr,d))
			return -1;
		m_data = d;
	}
	return 0;
}


int PCF8574::write(uint8_t v)
{
	log_dbug(TAG,"write(%x)",v);
	if (m_data != v) {
		if (i2c_write1(m_bus,m_addr,v))
			return -1;
		m_data = v;
	}
	return 0;
}


int PCF8574::write(uint8_t *v, unsigned n)
{
	log_dbug(TAG,"write(%p,%u)",v,n);
	if (i2c_write(m_bus,&m_addr,1,0,1))
		return -1;
	if (i2c_write(m_bus,v,n,1,0))
		return -1;
	m_data = v[n-1];
	return 0;
}


uint8_t PCF8574::read()
{
	uint8_t v;
	if (i2c_read(m_bus,m_addr,&v,sizeof(v)))
		return 0;
	return v;
}


/*
int PCF8574::write(uint8_t *v, unsigned n, int off)
{
	log_dbug(TAG,"write(%x...,%d)",*v,off);
	if (off < 0)
		off = m_pos;
	if (n + off > m_dim)
		n = m_dim - off;
	if (0 != memcmp(m_data+off,v,n)) {
		uint8_t data[] = { m_addr , (uint8_t)off };
		if (i2c_write(m_bus,data,sizeof(data),false,true))
			return -1;
		if (i2c_write(m_bus,v,n,true,false))
			return -1;
		memcpy(m_data+off,v,n);
	}
	off += n;
	if (off < m_dim)
		m_pos = off;
	else
		m_pos = 0;
	return 0;
}


int PCF8574::setOn(bool on)
{
	uint8_t cmd[] = { m_addr, (uint8_t)(CMD_SYSTEM | on) };
	return i2c_write(m_bus,cmd,sizeof(cmd),true,true);
}


int PCF8574::setBlink(uint8_t blink)
{
	uint8_t cmd[] = { m_addr, (uint8_t)(CMD_DISP_ON | 0) };
	return i2c_write(m_bus,cmd,sizeof(cmd),true,true);
}


int PCF8574::setNumDigits(unsigned n)
{
	if (n <= 16) {
		uint8_t cmd[] = { m_addr, (uint8_t)(CMD_DIM | (n-1)) };
		if (0 == i2c_write(m_bus,cmd,sizeof(cmd),true,true)) {
			m_dim = n;
			return 0;
		}
	}
	return -1;
}
*/


unsigned pcf8574_scan(uint8_t bus)
{
	return PCF8574::create(bus);
}

#endif
