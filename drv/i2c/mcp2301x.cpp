/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#if defined CONFIG_MCP2301X

#include "actions.h"
#include "mcp2301x.h"
#include "log.h"

#define DEV_ADDR_MIN	(0x20 << 1)
#define DEV_ADDR_MAX	(0x27 << 1)


#define REG0_IODIRA	0x00
#define REG0_IODIRB	0x01
#define REG0_IPOLA	0x02
#define REG0_IPOLB	0x03
#define REG0_GPINTENA	0x04
#define REG0_GPINTENB	0x05
#define REG0_DEFVALA	0x06
#define REG0_DEFVALB	0x07
#define REG0_INTCONA	0x08
#define REG0_INTCONB	0x09
#define REG0_IOCONA	0x0a
#define REG0_IOCONB	0x0b	// is actually IOCONA
#define REG0_GPPUA	0x0c
#define REG0_GPPUB	0x0d
#define REG0_INTFA	0x0e
#define REG0_INTFB	0x0f
#define REG0_INTCAPA	0x10
#define REG0_INTCAPB	0x11
#define REG0_GPIOA	0x12
#define REG0_GPIOB	0x13
#define REG0_OLATA	0x14
#define REG0_OLATB	0x15

#define REG1_IODIRA	0x00
#define REG1_IODIRB	0x10
#define REG1_IPOLA	0x01
#define REG1_IPOLB	0x11
#define REG1_GPINTENA	0x02
#define REG1_GPINTENB	0x12
#define REG1_DEFVALA	0x03
#define REG1_DEFVALB	0x13
#define REG1_INTCONA	0x04
#define REG1_INTCONB	0x14
#define REG1_IOCONA	0x05
#define REG1_IOCONB	0x15	// is actually IOCONA
#define REG1_GPPUA	0x06
#define REG1_GPPUB	0x16
#define REG1_INTFA	0x07
#define REG1_INTFB	0x17
#define REG1_INTCAPA	0x08
#define REG1_INTCAPB	0x18
#define REG1_GPIOA	0x09
#define REG1_GPIOB	0x19
#define REG1_OLATA	0x0a
#define REG1_OLATB	0x1a


#define TAG MODULE_MCP230XX

MCP2301X *MCP2301X::Instances = 0;


MCP2301X::MCP2301X(uint8_t b, uint8_t a, int8_t inta, int8_t intb)
: I2CDevice(b,a,"mcp2301x")
, m_next(Instances)
, m_bus(b)
, m_addr(a)
{
	Instances = this;
	// clear interrupt flags
	uint16_t dummy;
	i2c_w1rd(b,a,REG0_INTFA,(uint8_t*)&dummy,sizeof(dummy));
	// set interrupt output pin to open-drain, clear interrupt via INTCAP
	i2c_write2(b,a,REG0_IOCONA,0x41);
}


MCP2301X *MCP2301X::atAddr(uint8_t a)
{
	MCP2301X *i = Instances;
	while (i) {
		if (a == i->m_addr)
			return i;
		i = i->m_next;
	}
	return 0;
}


const char *MCP2301X::getName() const
{
	return m_name;
}


void MCP2301X::attach(class EnvObject *)
{

}


void MCP2301X::intrHandlerA(void *arg)
{
	MCP2301X *dev = (MCP2301X *)arg;
	if (dev->m_iaev)
		event_isr_trigger(dev->m_iaev);
}


void MCP2301X::intrHandlerB(void *arg)
{
	MCP2301X *dev = (MCP2301X *)arg;
	if (dev->m_ibev)
		event_isr_trigger(dev->m_ibev);
}


MCP2301X *MCP2301X::create(uint8_t bus, uint8_t addr, int8_t inta, int8_t intb)
{
	addr |= 0x40;
	log_dbug(TAG,"seraching for device at %u/%x",bus,addr);
	uint8_t data[16];
	memset(data,0x55,sizeof(data));
	int n = i2c_w1rd(bus, addr, REG0_IODIRA, data, sizeof(data));
	if (n) {
		log_warn(TAG,"no mcp2301x at %u,%u",bus,addr);
		return 0;
	}
	return new MCP2301X(bus,addr,inta,intb);
}


const char *MCP2301X::drvName() const
{
	return "mcp2301x";
}


int MCP2301X::set_reg_bit(uint8_t reg, uint8_t bit, bool value)
{
	log_dbug(TAG,"set_reg_bit %x,%u,%u",reg,bit,value);
	if (bit&8) {
		++reg;
		bit &= 7;
	}
	uint8_t data;
	if (i2c_w1rd(m_bus, m_addr, reg, &data, sizeof(data)))
		return -1;
	if (value)
		data |= 1 << bit;
	else
		data &= ~(1<<bit);
	return i2c_write2(m_bus,m_addr,reg,data);
}


int MCP2301X::get_dir(uint8_t io) const
{
	uint8_t reg = REG0_IODIRA;
	if (io & 8) {
		io &= 7;
		++reg;
	}
	uint8_t dir;
	if (i2c_w1rd(m_bus, m_addr, reg, &dir, sizeof(dir)))
		return -1;
	return ((dir >> io) & 1) ? xio_cfg_io_in : xio_cfg_io_od;
}


int MCP2301X::set_dir(uint8_t io, xio_cfg_io_t dir)
{
	log_dbug(TAG,"dir %u %u",io,dir);
	if (dir == xio_cfg_io_keep)
		return 0;
	if (dir == xio_cfg_io_out)
		return -1;
	if (dir == xio_cfg_io_in)
		m_dir |= (1 << io);
	else
		m_dir &= ~(1 << io);
	return set_reg_bit(REG0_IODIRA,io,dir == xio_cfg_io_in);
}


int MCP2301X::set_pullup(uint8_t io, xio_cfg_pull_t pull)
{
	if (pull == xio_cfg_pull_keep)
		return 0;
	if (pull == xio_cfg_pull_down)
		return -1;
	return set_reg_bit(REG0_GPPUA,io,pull == xio_cfg_pull_up);
}


int MCP2301X::set_intr_a(xio_t inta)
{
	event_t fev = xio_get_fallev(inta);
	if (fev != 0) {
	} else if (0 == xio_set_intr((xio_t)inta,intrHandlerA,(void*)this)) {
		fev = event_register(m_name,"`intr_a");
		m_iaev = fev;
	} else {
		log_warn(TAG,"xio%u isr hander",inta);
		return -1;
	}
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_intr = xio_cfg_intr_fall;
	cfg.cfg_pull = xio_cfg_pull_up;
	if (0 > xio_config(inta,cfg)) {
		log_warn(TAG,"xio%u cannot be used as interrupt source");
		return -1;
	}
	Action *a = action_add(concat(m_name,"!hdl_intra"),eval_intrA,this,0);
	event_callback(fev,a);
	log_dbug(TAG,"intr_a %u, event %u",inta,fev);
	return 0;
}


int MCP2301X::set_intr_b(xio_t intb)
{
	event_t fev = xio_get_fallev(intb);
	if (fev != 0) {
	} else if (0 == xio_set_intr((xio_t)intb,intrHandlerB,(void*)this)) {
		fev = event_register(m_name,"`intr_b");
		m_ibev = fev;
	} else {
		log_warn(TAG,"xio%u isr hander",intb);
		return -1;
	}
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_intr = xio_cfg_intr_fall;
	cfg.cfg_pull = xio_cfg_pull_up;
	if (0 > xio_config(intb,cfg)) {
		log_warn(TAG,"xio%u cannot be used as interrupt source");
		return -1;
	}
	Action *a = action_add(concat(m_name,"!hdl_intrb"),eval_intrB,this,0);
	event_callback(fev,a);
	log_dbug(TAG,"intr_b %u",intb);
	return 0;
}


int MCP2301X::set_intr(uint8_t io, xio_cfg_intr_t intr)
{
	int r = 0;
	if (intr == xio_cfg_intr_keep) {
	} else if (intr == xio_cfg_intr_edges) {
		if (set_reg_bit(REG0_GPINTENA,io,true))	// enable
			r = -1;
		else if (set_reg_bit(REG0_INTCONA,io,false))	// edge trigger
			r = -1;
	} else if (intr == xio_cfg_intr_disable) {
		if (set_reg_bit(REG0_GPINTENA,io,false))	// enable
			r = -1;
	} else if (intr == xio_cfg_intr_lvl0) {
		if (set_reg_bit(REG0_GPINTENA,io,true))		// enable
			r = -1;
		else if (set_reg_bit(REG0_INTCONA,io,true))	// level trigger
			r = -1;
		else if (set_reg_bit(REG0_DEFVALA,io,false))	// level 0
			r = -1;
	} else if (intr == xio_cfg_intr_lvl1) {
		if (set_reg_bit(REG0_GPINTENA,io,true))		// enable
			r = -1;
		else if (set_reg_bit(REG0_INTCONA,io,true))	// level trigger
			r = -1;
		else if (set_reg_bit(REG0_DEFVALA,io,true))	// level 0
			r = -1;
	} else {
		r = -1;
	}
	return r;
}


int MCP2301X::set_pullups(uint16_t pullups)
{
	uint8_t data[] = { m_addr, REG0_GPPUA, (uint8_t)pullups, (uint8_t)(pullups>>8) };
	return i2c_write(m_bus,data,sizeof(data),true,true);
}


int MCP2301X::config(uint8_t gpio, xio_cfg_t cfg)
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


int MCP2301X::get_lvl(uint8_t io)
{
	uint8_t reg = REG0_GPIOA;
	if (io & 8) {
		io &= 7;
		++reg;
	}
	if (m_dir & (1<<io))	// if set to output read OLAT
		reg += 2;
	uint8_t v;
	int r = i2c_w1rd(m_bus,m_addr,reg,&v,sizeof(v));
	if (r < 0)
		return -1;
	return (v >> io) & 1;
}


int MCP2301X::get_pending(uint16_t *p)
{
	uint8_t d[2] = {0};
	int r = i2c_w1rd(m_bus,m_addr,REG0_INTFA,d,sizeof(d));
	*p = d[0] | (d[1]<<8);
	return r;
}


int MCP2301X::get_in(uint16_t *p)
{
	uint8_t d[2] = {0};
	int r = i2c_w1rd(m_bus,m_addr,REG0_GPIOA,d,sizeof(d));
	*p = d[0] | (d[1]<<8);
	return r;
}


int MCP2301X::set_out(uint16_t p)
{
	uint8_t data[] = { m_addr, REG0_GPIOA, (uint8_t)p, (uint8_t)(p>>8) };
	return i2c_write(m_bus,data,sizeof(data),true,true);
}


int MCP2301X::set_lvl(uint8_t io, xio_lvl_t l)
{
	bool v;
	uint8_t r = REG0_GPIOA;
	if (io & 8) {
		++r;
		io &= 7;
	}
	if (l == xio_lvl_0) {
		v = false;
	} else if (l == xio_lvl_1) {
		v = true;
	} else {
	return -1;
	}
	return set_reg_bit(r,io,v);
}


int MCP2301X::set_lo(uint8_t io)
{
	return set_lvl(io,xio_lvl_0);
}


int MCP2301X::set_hi(uint8_t io)
{
	return set_lvl(io,xio_lvl_1);
}


int MCP2301X::setm(uint32_t values,uint32_t mask)
{
	if (mask >> 16)
		return -1;
	if ((values ^ mask) & values)
		return -1;
	uint8_t data[4];
	if (i2c_w1rd(m_bus, m_addr, REG0_GPIOA, data+2, 2))
		return -1;
	data[2] &= ~mask;
	data[3] &= ~(mask>>8);
	data[2] |= values;
	data[3] |= values>>8;
	data[1] = REG0_GPIOA;
	data[0] = m_addr;
	return i2c_write(m_bus,data,sizeof(data),true,true);
}


void MCP2301X::eval_intrA(void *arg)
{
	log_dbug(TAG,"eval intrA");
	MCP2301X *dev = (MCP2301X *)arg;
	uint8_t d[2] = {0};
	if (i2c_w1rd(dev->m_bus,dev->m_addr,REG0_INTFA,d,dev->m_ibev == 0 ? 2 : 1)) {
		log_warn(TAG,"clear flags");
		return;
	}
	uint16_t intr = d[0] | (d[1] << 8);
	if (i2c_w1rd(dev->m_bus,dev->m_addr,REG0_INTCAPA,d,dev->m_ibev == 0 ? 2 : 1)) {
		log_warn(TAG,"read capture");
		return;
	}
	uint16_t cap = d[0] | (d[1]<<8);
	uint16_t b = 1;
	uint8_t x = 0;
	while (intr) {
		if (intr & b) {
			intr ^= b;
			event_t ev = (cap & b) ? dev->m_riseev[x] : dev->m_fallev[x];
			if (ev)
				event_trigger(ev);
		}
		b <<= 1;
		++x;
	}
}


void MCP2301X::eval_intrB(void *arg)
{
	log_dbug(TAG,"eval intrB");
	MCP2301X *dev = (MCP2301X *)arg;
	uint8_t intr;
	if (i2c_w1rd(dev->m_bus,dev->m_addr,REG0_INTFB,&intr,1)) {
		log_warn(TAG,"clear flags");
		return;
	}
	uint8_t cap;
	if (i2c_w1rd(dev->m_bus,dev->m_addr,REG0_INTCAPB,&cap,1)) {
		log_warn(TAG,"read capture");
		return;
	}
	uint8_t b = 1;
	uint8_t x = 8;
	while (intr) {
		if (intr & b) {
			intr ^= b;
			event_t ev = (cap & b) ? dev->m_riseev[x] : dev->m_fallev[x];
			if (ev)
				event_trigger(ev);
		}
		b <<= 1;
		++x;
	}
}



event_t MCP2301X::get_fallev(uint8_t io)
{
	if (io >= 16)
		return 0;
	if (0 == m_fallev[io]) {
		char evname[32];
		sprintf(evname,"%s:%u",m_name,io);
		m_fallev[io] = event_register(evname,"`fall");
	}
	return m_fallev[io];
}


event_t MCP2301X::get_riseev(uint8_t io)
{
	if (io >= 16)
		return 0;
	if (0 == m_riseev[io]) {
		char evname[32];
		sprintf(evname,"%s:%u",m_name,io);
		m_riseev[io] = event_register(evname,"`rise");
	}
	return m_riseev[io];
}

#endif
