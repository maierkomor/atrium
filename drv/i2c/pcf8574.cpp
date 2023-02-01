/*
 *  Copyright (C) 2021-2022, Thomas Maier-Komor
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

PCF8574 *PCF8574::create(uint8_t bus, uint8_t addr)
{
	uint8_t data;
	if (i2c_read(bus,addr<<1,&data,sizeof(data))) {
		log_warn(TAG,"no response from %u/0x%x",bus,addr);
		return 0;
	}
	log_dbug(TAG,"device at %u/0x%x",bus,addr);
	PCF8574 *dev = new PCF8574(bus,addr<<1);
	if (Instance) {
		PCF8574 *i = Instance;
		while (i->m_next)
			i = i->m_next;
		i->m_next = dev;
	} else {
		Instance = dev;
	}
	return dev;
}


const char *PCF8574::drvName() const
{
	return "pcf8574";
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


#ifdef CONFIG_IOEXTENDERS

int PCF8574::get_lvl(uint8_t io)
{
	if (io >> 3)
		return -1;
	uint8_t v;
	if (i2c_read(m_bus,m_addr,&v,sizeof(v)))
		return -1;
	return (v >> io) & 1;
}


int PCF8574::set_hi(uint8_t io)
{
	if (io >> 3)
		return -1;
	uint8_t v;
	if (i2c_read(m_bus,m_addr,&v,sizeof(v)))
		return -1;
	v |= (1 << io);
	return i2c_write1(m_bus,m_addr,v);
}


int PCF8574::set_lo(uint8_t io)
{
	if (io >> 3)
		return -1;
	uint8_t v;
	if (i2c_read(m_bus,m_addr,&v,sizeof(v)))
		return -1;
	v &= ~(1 << io);
	return i2c_write1(m_bus,m_addr,v);

}


int PCF8574::set_intr(uint8_t, xio_intrhdlr_t, void*)
{
	return -1;
}


int PCF8574::config(uint8_t io, xio_cfg_t cfg)
{
	log_dbug(TAG,"config %x",cfg);
	if (io >> 3)
		return -1;
	if (cfg.cfg_io == xio_cfg_io_keep) {
	} else if (cfg.cfg_io == xio_cfg_io_in) {
		set_hi(io);
	} else if (cfg.cfg_io == xio_cfg_io_out) {
	} else if (cfg.cfg_io == xio_cfg_io_od) {
		return -1;
	}

	if ((cfg.cfg_pull != xio_cfg_pull_keep) && (cfg.cfg_pull != xio_cfg_pull_none))
		return -1;

	if ((cfg.cfg_intr != xio_cfg_intr_keep) && (cfg.cfg_intr != xio_cfg_intr_disable))
		return -1;

	if ((cfg.cfg_wakeup != xio_cfg_wakeup_keep) && (cfg.cfg_wakeup != xio_cfg_wakeup_disable))
		return -1;
	return xio_cap_edgeintr|xio_cap_lvl0intr|xio_cap_lvl1intr;

}


int PCF8574::set_lvl(uint8_t io, xio_lvl_t v)
{
	if (v == xio_lvl_0)
		return set_lo(io);
	if (v == xio_lvl_1)
		return set_hi(io);
	return -1;
}


const char *PCF8574::getName() const
{
	return m_name;
}


int PCF8574::get_dir(uint8_t num) const
{
	return xio_cfg_io_od;
}
#endif	// CONFIG_IOEXTENDERS


#endif
