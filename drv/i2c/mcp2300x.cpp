/*
 *  Copyright (C) 2022-2024, Thomas Maier-Komor
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

#if defined CONFIG_MCP2300X

#include "actions.h"
#include "mcp2300x.h"
#include "log.h"


#define DEV_ADDR_MIN	(0x20 << 1)
#define DEV_ADDR_MAX	(0x27 << 1)

#define REG_IODIR	0x00
#define REG_IPOL	0x01
#define REG_GPINTEN	0x02
#define REG_DEFVAL	0x03
#define REG_INTCON	0x04
#define REG_IOCON	0x05
#define REG_GPPU	0x06
#define REG_INTF	0x07
#define REG_INTCAP	0x08
#define REG_GPIO	0x09
#define REG_OLAT	0x0a


#define TAG MODULE_MCP230XX

MCP2300X *MCP2300X::Instances = 0;


MCP2300X::MCP2300X(uint8_t b, uint8_t a, int8_t inta)
: I2CDevice(b,a,"mcp2300x")
, m_next(Instances)
, m_bus(b)
, m_addr(a)
{
	Instances = this;
	// clear interrupt flags
	uint8_t dummy;
	i2c_w1rd(b,a,REG_INTF,&dummy,sizeof(dummy));
	// set interrupt output pin to open-drain, clear interrupt via INTCAP
	i2c_write2(b,a,REG_IOCON,0x41);
}


MCP2300X *MCP2300X::atAddr(uint8_t a)
{
	MCP2300X *i = Instances;
	while (i) {
		if (a == i->m_addr)
			return i;
		i = i->m_next;
	}
	return 0;
}


const char *MCP2300X::getName() const
{
	return m_name;
}


void MCP2300X::attach(class EnvObject *)
{

}


MCP2300X *MCP2300X::create(uint8_t bus, uint8_t addr, int8_t inta)
{
	addr |= 0x40;
	log_dbug(TAG,"seraching for device at %u/%x",bus,addr);
	uint8_t data[8];
	memset(data,0x55,sizeof(data));
	int n = i2c_w1rd(bus, addr, REG_IODIR, data, sizeof(data));
	if (n) {
		log_warn(TAG,"no mcp2300x at %u,%u",bus,addr);
		return 0;
	}
	return new MCP2300X(bus,addr,inta);
}


const char *MCP2300X::drvName() const
{
	return "mcp2300x";
}


int MCP2300X::set_reg_bit(uint8_t reg, uint8_t bit, bool value)
{
	log_dbug(TAG,"set_reg_bit %x,%u,%u",reg,bit,value);
	uint8_t data;
	if (i2c_w1rd(m_bus, m_addr, reg, &data, sizeof(data)))
		return -1;
	if (value)
		data |= 1 << bit;
	else
		data &= ~(1<<bit);
	return i2c_write2(m_bus,m_addr,reg,data);
}


int MCP2300X::get_dir(uint8_t io) const
{
	uint8_t dir;
	if (i2c_w1rd(m_bus, m_addr, REG_IODIR, &dir, sizeof(dir)))
		return -1;
	return ((dir >> io) & 1) ? xio_cfg_io_in : xio_cfg_io_od;
}


int MCP2300X::set_dir(uint8_t io, xio_cfg_io_t dir)
{
	log_dbug(TAG,"dir %u %u",io,dir);
	if (dir == xio_cfg_io_keep)
		return 0;
	if (dir == xio_cfg_io_out)
		return -1;
	if (dir == xio_cfg_io_in)
		m_dir |= 1 << io;
	else
		m_dir &= ~(1 << io);
	return set_reg_bit(REG_IODIR,io,dir == xio_cfg_io_in);
}


int MCP2300X::set_pullup(uint8_t io, xio_cfg_pull_t pull)
{
	if (pull == xio_cfg_pull_keep)
		return 0;
	if (pull == xio_cfg_pull_down)
		return -1;
	return set_reg_bit(REG_GPPU,io,pull == xio_cfg_pull_up);
}


int MCP2300X::set_intr_a(xio_t inta)
{
	event_t fev = xio_get_fallev(inta);
	if (fev == 0) {
		fev = event_register(m_name,"`intr_a");
		if (0 == xio_set_intr((xio_t)inta,event_isr_handler,(void*)(unsigned)m_iev)) {
			m_iev = fev;
		} else {
			log_warn(TAG,"xio%u isr hander",inta);
			return -1;
		}
	}
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_intr = xio_cfg_intr_fall;
	cfg.cfg_pull = xio_cfg_pull_up;
	if (0 > xio_config(inta,cfg)) {
		log_warn(TAG,"xio%u cannot be used as interrupt source");
		return -1;
	}
	Action *a = action_add(concat(m_name,"!hdl_intr"),eval_intr,this,0);
	event_callback(fev,a);
	log_dbug(TAG,"intr_a %u, event %u",inta,fev);
	return 0;
}


int MCP2300X::set_intr(uint8_t io, xio_cfg_intr_t intr)
{
	int r = 0;
	if (intr == xio_cfg_intr_keep) {
	} else if (intr == xio_cfg_intr_edges) {
		if (set_reg_bit(REG_GPINTEN,io,true))	// enable
			r = -1;
		else if (set_reg_bit(REG_INTCON,io,false))	// edge trigger
			r = -1;
	} else if (intr == xio_cfg_intr_disable) {
		if (set_reg_bit(REG_GPINTEN,io,false))	// enable
			r = -1;
	} else if (intr == xio_cfg_intr_lvl0) {
		if (set_reg_bit(REG_GPINTEN,io,true))		// enable
			r = -1;
		else if (set_reg_bit(REG_INTCON,io,true))	// level trigger
			r = -1;
		else if (set_reg_bit(REG_DEFVAL,io,false))	// level 0
			r = -1;
	} else if (intr == xio_cfg_intr_lvl1) {
		if (set_reg_bit(REG_GPINTEN,io,true))		// enable
			r = -1;
		else if (set_reg_bit(REG_INTCON,io,true))	// level trigger
			r = -1;
		else if (set_reg_bit(REG_DEFVAL,io,true))	// level 0
			r = -1;
	} else {
		r = -1;
	}
	return r;
}


int MCP2300X::set_pullups(uint8_t pullups)
{
	uint8_t data[] = { m_addr, REG_GPPU, pullups };
	return i2c_write(m_bus,data,sizeof(data),true,true);
}


int MCP2300X::config(uint8_t gpio, xio_cfg_t cfg)
{
	log_dbug(TAG,"config %x",cfg);
	if (cfg.cfg_io != xio_cfg_io_keep) {
		if (set_dir(gpio,(xio_cfg_io_t)cfg.cfg_io))
			return -1;
	}

	if (cfg.cfg_pull != xio_cfg_pull_keep) {
		if (set_pullup(gpio,cfg.cfg_pull))
			return -1;
	}

	if (cfg.cfg_intr != xio_cfg_intr_keep) {
		if (set_intr(gpio,cfg.cfg_intr))
			return -1;
	}

	if (cfg.cfg_wakeup != xio_cfg_wakeup_keep)
		return -1;
	return xio_cap_pullup|xio_cap_edgeintr|xio_cap_lvl0intr|xio_cap_lvl1intr;
}


int MCP2300X::get_lvl(uint8_t io)
{
	if (io >= 8)
		return -1;
	uint8_t v;
	int r;
	if (m_dir & (1<<io))
		r = i2c_w1rd(m_bus,m_addr,REG_GPIO,&v,sizeof(v));
	else
		r = i2c_w1rd(m_bus,m_addr,REG_OLAT,&v,sizeof(v));
	if (r < 0)
		return -1;
	return (v >> io) & 1;
}


int MCP2300X::set_lvl(uint8_t io, xio_lvl_t l)
{
	if (io >= 8)
		return -1;
	bool v;
	if (l == xio_lvl_0) {
		v = false;
	} else if (l == xio_lvl_1) {
		v = true;
	} else {
		return -1;
	}
	return set_reg_bit(REG_GPIO,io,v);
}


int MCP2300X::set_lo(uint8_t io)
{
	if (io < 8)
		return set_reg_bit(REG_GPIO,io,0);
	return -1;
}


int MCP2300X::set_hi(uint8_t io)
{
	if (io < 8)
		return set_reg_bit(REG_GPIO,io,1);
	return -1;
}


int MCP2300X::setm(uint32_t values,uint32_t mask)
{
	if (mask >> 8)
		return -1;
	if ((values ^ mask) & values)
		return -1;
	uint8_t data;
	if (i2c_w1rd(m_bus, m_addr, REG_GPIO, &data, 1))
		return -1;
	data &= ~mask;
	data |= values;
	return i2c_write2(m_bus,m_addr,REG_GPIO,data);
}


void MCP2300X::eval_intr(void *arg)
{
	MCP2300X *dev = (MCP2300X *)arg;
	uint8_t intr;
	if (i2c_w1rd(dev->m_bus,dev->m_addr,REG_INTF,&intr,1)) {
		log_warn(TAG,"clear flags");
		return;
	}
	uint8_t cap;
	if (i2c_w1rd(dev->m_bus,dev->m_addr,REG_INTCAP,&cap,1)) {
		log_warn(TAG,"read capture");
		return;
	}
	log_dbug(TAG,"eval %x,%x",intr,cap);
	uint8_t b = 1;
	uint8_t x = 0;
	while (intr) {
		if (intr & b) {
			intr ^= b;
			event_t ev = (cap & b) ? dev->m_riseev[x] : dev->m_fallev[x];
			if (ev) {
				log_dbug(TAG,"event %u on %u",ev,x);
				event_trigger(ev);
			} else
				log_dbug(TAG,"no event on %u",x);
		}
		b <<= 1;
		++x;
	}
}


event_t MCP2300X::get_fallev(uint8_t io)
{
	if (io >= 8)
		return 0;
	if (0 == m_fallev[io]) {
		char evname[32];
		sprintf(evname,"%s:%u",m_name,io);
		m_fallev[io] = event_register(evname,"`fall");
	}
	return m_fallev[io];
}


event_t MCP2300X::get_riseev(uint8_t io)
{
	if (io >= 8)
		return 0;
	if (0 == m_riseev[io]) {
		char evname[32];
		sprintf(evname,"%s:%u",m_name,io);
		m_riseev[io] = event_register(evname,"`rise");
	}
	return m_riseev[io];
}


#endif
