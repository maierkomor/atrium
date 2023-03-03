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

#if defined CONFIG_TCA9555

#include "log.h"
#include "tca9555.h"

#include <string.h>


#define DEV_ADDR_MIN	0x20
#define DEV_ADDR_MAX	0x28	// out of range

#define REG_IN_0	0
#define REG_IN_1	1
#define REG_OUT_0	2
#define REG_OUT_1	3
#define REG_POL_0	4
#define REG_POL_1	5
#define REG_CFG_0	6
#define REG_CFG_1	7

#define TAG MODULE_TCA9555

TCA9555 *TCA9555::Instance = 0;

// 8 GPIOs
// set to hi for use as input!

TCA9555 *TCA9555::create(uint8_t bus, uint8_t addr)
{
	uint8_t data[6];
	if ((addr < DEV_ADDR_MIN) || (addr >= DEV_ADDR_MAX)) {
		log_warn(TAG,"address %x out of range",addr);
		return 0;
	}
	addr <<= 1;
	if (i2c_w1rd(bus,addr,REG_CFG_0,&data[0],2)) {
		log_warn(TAG,"no response from %u/0x%x",bus,addr);
		return 0;
	}
	log_info(TAG,"device at %x",addr);
	// restore power-off state after reset
	uint8_t init[] = { addr, REG_OUT_0, 0xff, 0xff, 0x0, 0x0, 0xff, 0xff };
	if (i2c_write(bus,init,sizeof(init),1,1))
		log_warn(TAG,"reset failed");
	log_dbug(TAG,"device at %u/0x%x",bus,addr);
	TCA9555 *dev = new TCA9555(bus,addr);
	if (Instance) {
		TCA9555 *i = Instance;
		while (i->m_next)
			i = i->m_next;
		i->m_next = dev;
	} else {
		Instance = dev;
	}
	return dev;
}


const char *TCA9555::drvName() const
{
	return "tca9555";
}


int TCA9555::setGpio(bool value, unsigned off)
{
	if (off >> 4)
		return -1;
	if (((m_out >> off) & 1) != value) {
		uint16_t d = m_out;
		if (value)
			d |= (1<<off);
		else
			d &= ~(1<<off);
		if (i2c_write2(m_bus,m_addr,off & 8 ? REG_OUT_1 : REG_OUT_0, (uint8_t)(off & 8 ? d >> 8 : d)))
			return -1;
		m_out = d;
	}
	return 0;
}


int TCA9555::write(uint16_t v)
{
	log_dbug(TAG,"write(%x)",v);
	if (m_out != v) {
		uint8_t data[] = { m_addr, REG_OUT_0, (uint8_t)v, (uint8_t)(v >> 8) };
		if (i2c_write(m_bus,data,sizeof(data),1,1))
			return -1;
		m_out = v;
	}
	return 0;
}


int TCA9555::write(uint8_t *v, unsigned n)
{
	log_dbug(TAG,"write(%p,%u)",v,n);
	uint8_t data[] = { m_addr, REG_OUT_0 };
	if (i2c_write(m_bus,data,sizeof(data),0,1))
		return -1;
	if (i2c_write(m_bus,v,sizeof(v),1,0))
		return -1;
	m_out = v[n-1];
	return 0;
}


uint16_t TCA9555::read()
{
	uint16_t v;
	if (i2c_w1rd(m_bus,m_addr,REG_IN_0,(uint8_t*)&v,sizeof(v)))
		return 0;
	return v;
}


#ifdef CONFIG_IOEXTENDERS

int TCA9555::get_lvl(uint8_t io)
{
	if (io >> 4)
		return -1;
	uint8_t r = io & 0x8 ? REG_IN_1 : REG_IN_0;
	io &= 7;
	uint8_t v;
	if (i2c_w1rd(m_bus,m_addr,r,&v,sizeof(v)))
		return -1;
	return (v >> io) & 1;
}


int TCA9555::set_hi(uint8_t io)
{
	if (io >> 4)
		return -1;
	if ((m_out >> io) & 1)
		return 0;
	m_out |= 1 << io;
	uint8_t data[] = {
		m_addr,
		(uint8_t)(io & 8 ? REG_OUT_1 : REG_OUT_0),
		(uint8_t)(io & 8 ? m_out >> 8 : m_out),
	};
	return i2c_write(m_bus,data,sizeof(data),1,1);
}


int TCA9555::set_lo(uint8_t io)
{
	if (io >> 4)
		return -1;
	if (((m_out >> io) & 1) == 0)
		return 0;
	m_out &= ~(1 << io);
	uint8_t data[] = {
		m_addr,
		(uint8_t)(io & 8 ? REG_OUT_1 : REG_OUT_0),
		(uint8_t)(io & 8 ? m_out >> 8 : m_out)
	};
	return i2c_write(m_bus,data,sizeof(data),1,1);
}


int TCA9555::set_intr(uint8_t, xio_intrhdlr_t, void*)
{
	return -1;
}


int TCA9555::config(uint8_t io, xio_cfg_t cfg)
{
	log_dbug(TAG,"config %d,0x%x",io,cfg);
	if (io >> 4)
		return -1;
	if (cfg.cfg_io == xio_cfg_io_keep) {
		log_dbug(TAG,"io keep");
	} else if (cfg.cfg_io == xio_cfg_io_in) {
		if ((m_cfg & (1 << io)) == 0) {
			m_cfg |= (1 << io);
			uint8_t data[] = {
				m_addr,
				(uint8_t)(io & 8 ? REG_CFG_1 : REG_CFG_0),
				(uint8_t)(io & 8 ? m_cfg >> 8 : m_cfg)
			};
			log_dbug(TAG,"io in");
			i2c_write(m_bus,data,sizeof(data),1,1);
		}
	} else if (cfg.cfg_io == xio_cfg_io_out) {
		if ((m_cfg & (1 << io)) != 0) {
			m_cfg &= ~(1 << io);
			uint8_t data[] = {
				m_addr,
				(uint8_t)(io & 8 ? REG_CFG_1 : REG_CFG_0),
				(uint8_t)(io & 8 ? m_cfg >> 8 : m_cfg)
			};
			log_dbug(TAG,"io out");
			i2c_write(m_bus,data,sizeof(data),1,1);
		}
	} else if (cfg.cfg_io == xio_cfg_io_od) {
		return -1;
	}

	if ((cfg.cfg_pull != xio_cfg_pull_keep) && (cfg.cfg_pull != xio_cfg_pull_none))
		return -1;

	if ((cfg.cfg_intr != xio_cfg_intr_keep) && (cfg.cfg_intr != xio_cfg_intr_disable))
		return -1;

	if ((cfg.cfg_wakeup != xio_cfg_wakeup_keep) && (cfg.cfg_wakeup != xio_cfg_wakeup_disable))
		return -1;
	return xio_cap_none;

}


int TCA9555::set_lvl(uint8_t io, xio_lvl_t v)
{
	if (v == xio_lvl_0)
		return set_lo(io);
	if (v == xio_lvl_1)
		return set_hi(io);
	return -1;
}


const char *TCA9555::getName() const
{
	return m_name;
}


int TCA9555::get_dir(uint8_t num) const
{
	if (num >> 4)
		return -1;
	return ((m_cfg >> num) & 1) ? xio_cfg_io_in : xio_cfg_io_out;
}
#endif	// CONFIG_IOEXTENDERS


#endif
