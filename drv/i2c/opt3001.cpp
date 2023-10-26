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

#ifdef CONFIG_OPT3001

#include "actions.h"
#include "cyclic.h"
#include "opt3001.h"
#include "log.h"
#include "xio.h"

#define REG_RESULT	0x00
#define REG_CONFIG	0x01
#define REG_LOWLIM	0x02
#define REG_HIGHLIM	0x03
#define REG_MANID	0x7e
#define REG_DEVID	0x7f

#define DEV_ADDR_LOW	0x44
#define DEV_ADDR_HIGH	0x47

#define BITS_EXP	0xf000
#define BITS_FRAC	0x0fff
#define BITS_RANGE	0xf000
#define BITS_CONVTIME	(1<<11)
#define BITS_MODE	0x0600
#define BITS_OVFL	0x0100
#define BITS_RDY	0x0080
#define BITS_FLAGHIGH	0x0840
#define BITS_FLAGLOW	0x0820
#define BITS_LATCH	0x0010
#define BITS_POL	0x0008
#define BITS_MASKEXP	0x0004
#define BITS_FAULTCNT	0x0003

#define MODE_OFF	0x0000
#define MODE_SINGLE	0x0200
#define MODE_CONT	0x0400

#define TAG MODULE_OPT3001


static const float Ranges[] =
	{ 40.95, 81.90, 163.8, 327.6, 655.2, 1310.4, 2620.8
	, 5241.6, 10483.2, 20966.4, 41932.8, 89865.6
};


OPT3001::OPT3001(unsigned bus, unsigned addr)
: I2CDevice(bus,addr,drvName())
, m_lum("luminance","lux")
{
	m_lum.set(NAN);
}


void OPT3001::addIntr(uint8_t gpio)
{
	m_isrev = event_register(m_name,"`isr");
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = xio_cfg_pull_up;
	cfg.cfg_intr = xio_cfg_intr_fall;
	if (0 > xio_config(gpio,cfg)) {
		log_warn(TAG,"gpio %u as interrupt failed",gpio);
	} else if (xio_set_intr(gpio,intrHandler,this)) {
		log_warn(TAG,"add handler for gpio %u interrupt failed",gpio);
	}
}

void OPT3001::attach(EnvObject *root)
{
	root->add(&m_lum);
	cyclic_add_task(m_name,OPT3001::cyclic,this,0);
	action_add(concat(m_name,"!sample"),sample,(void*)this,"OPT3001 sample data");
}


unsigned OPT3001::cyclic(void *arg)
{
	OPT3001 *dev = (OPT3001 *) arg;
	dev->read();
	return 1000;
}


OPT3001 *OPT3001::create(unsigned bus, unsigned addr)
{
	uint8_t data[2];
	if (esp_err_t e = i2c_w1rd(bus,addr,REG_MANID,data,sizeof(data))) {
		log_warn(TAG,"cannot read manufacturer id: %s",esp_err_to_name(e));
		return 0;
	}
	log_info(TAG,"manufacturer id 0x%x",data[0] | (data[1]<<8));
	if (esp_err_t e = i2c_w1rd(bus,addr,REG_DEVID,data,sizeof(data))) {
		log_warn(TAG,"cannot read device id: %s",esp_err_to_name(e));
		return 0;
	}
	log_info(TAG,"device id 0x%x",data[0] | (data[1]<<8));
	return new OPT3001(bus,addr);
}


#ifdef CONFIG_I2C_XCMD
const char *OPT3001::exeCmd(Terminal &term, int argc, const char **args)
{
	// TODO
	return 0;
}
#endif


int OPT3001::init()
{
	// configure to contiuous sampling
	// 800ms sampling time
	uint8_t cfg[] = { m_addr, REG_CONFIG, 0xce, 0x00 };
	if (esp_err_t e = i2c_write(m_bus,cfg,sizeof(cfg),1,1)) {
		log_warn(TAG,"failed to config device: %s",esp_err_to_name(e));
		return 1;
	}
	return 0;
}


void OPT3001::intrHandler(void *arg)
{
	OPT3001 *dev = (OPT3001 *) arg;
	event_isr_trigger(dev->m_isrev);
}


int OPT3001::read()
{
	uint8_t data[2] = { 0, 0 };
	if (int r = i2c_w1rd(m_bus,m_addr,REG_CONFIG,data,sizeof(data))) {
		log_warn(TAG,"read error: %s",esp_err_to_name(r));
		m_lum.set(NAN);
		return r;
	}
	log_dbug(TAG,"status: %s%s%s"
		, data[1]&0x80?" CRF":""
		, data[1]&0x40?" FH":""
		, data[1]&0x20?" FL":""
		);
	if (int r = i2c_w1rd(m_bus,m_addr,REG_RESULT,data,sizeof(data))) {
		log_warn(TAG,"read error: %s",esp_err_to_name(r));
		m_lum.set(NAN);
		return r;
	}
	log_dbug(TAG,"read: %02x %02x",data[0],data[1]);
	uint16_t val = ((uint16_t)data[0] << 8) | data[1];
	uint8_t exp = val >> 12;
	val &= 0xfff;
	float scale = ((float)(1<<exp))/100.0;
	float lum = (float)val * scale;
	m_lum.set(lum);
	return 0;
}


void OPT3001::sample(void *arg)
{
	OPT3001 *dev = (OPT3001 *) arg;
	dev->read();
}


#endif // CONFIG_OPT3001
