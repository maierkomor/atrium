/*
 *  Copyright (C) 2023-2024, Thomas Maier-Komor
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
#include "nvm.h"
#include "terminal.h"
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
#define BITS_OVERFLOW	0x0100
#define BITS_READY	0x0080
#define BITS_FLAGHIGH	0x0840
#define BITS_FLAGLOW	0x0820
#define BITS_LATCH	0x0010
#define BITS_POLARITY	0x0008
#define BITS_MASKEXP	0x0004
#define BITS_FAULTCNT	0x0003

#define BIT_FR		0x80	// conversion ready
#define BIT_FH		0x40	// flag high
#define BIT_FL		0x20	// flag low

#define SHIFT_MODE	9
#define SHIFT_RANGE	12

#define MODE_OFF	0x0000
#define MODE_SINGLE	0x0200
#define MODE_CONT	0x0400

#define MASK_CONFIG	0xfe1f	// mask-out r/o bits

#define TAG MODULE_OPT3001


static const float Ranges[] =
	{ 40.95, 81.90, 163.8, 327.6, 655.2, 1310.4, 2620.8
	, 5241.6, 10483.2, 20966.4, 41932.8, 89865.6
};


static float val2float(uint16_t v)
{
	uint8_t exp = v >> 12;
	float lsb = (1<<exp)/100;
	return (v & 0xfff) * lsb;
}


OPT3001::OPT3001(unsigned bus, unsigned addr)
: I2CDevice(bus,addr,drvName())
, m_lum("luminance","lux")
, m_cfg(0xce00)
{
	m_lum.set(NAN);
}


void OPT3001::addIntr(uint8_t gpio)
{
	log_dbug(TAG,"addIntr");
	m_isrev = event_register(m_name,"`isr");
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = xio_cfg_pull_up;
	cfg.cfg_intr = xio_cfg_intr_fall;
	if (0 > xio_config(gpio,cfg)) {
		log_warn(TAG,"gpio %u as interrupt failed",gpio);
	} else if (xio_set_intr(gpio,event_isr_handler,(void*)(unsigned)m_isrev)) {
		log_warn(TAG,"add handler for gpio %u interrupt failed",gpio);
	} else {
		m_cfg |= BITS_LATCH;
		uint8_t out[] = { m_addr, REG_LOWLIM, 0xc0, 0x00 };
		i2c_write(m_bus,out,sizeof(out),1,1);
		read();
	}
}


void OPT3001::attach(EnvObject *root)
{
	if (init())
		return;
	root->add(&m_lum);
	action_add(concat(m_name,"!sample"),single,(void*)this,"OPT3001 single sampling of data");
	action_add(concat(m_name,"!cont"),cont,(void*)this,"OPT3001 sample continuously");
	action_add(concat(m_name,"!stop"),stop,(void*)this,"OPT3001 stop sampling");
	if (m_isrev) {
		Action *a = action_add(concat(m_name,"!read"),read,(void*)this,0);
		event_callback(m_isrev,a);
	} else {
		cyclic_add_task(m_name,OPT3001::cyclic,this,0);
	}
}


unsigned OPT3001::cyclic(void *arg)
{
	OPT3001 *dev = (OPT3001 *) arg;
	return dev->read();
}


OPT3001 *OPT3001::create(unsigned bus, unsigned addr)
{
	uint8_t mid[2], did[2];
	if (esp_err_t e = i2c_w1rd(bus,addr,REG_MANID,mid,sizeof(mid))) {
		log_warn(TAG,"cannot read manufacturer id: %s",esp_err_to_name(e));
	} else if (esp_err_t e = i2c_w1rd(bus,addr,REG_DEVID,did,sizeof(did))) {
		log_warn(TAG,"cannot read device id: %s",esp_err_to_name(e));
	} else {
		log_info(TAG,"manufacturer id 0x%x, device id 0x%x", mid[0]|(mid[1]<<8), did[0]|(did[1]<<8));
		return new OPT3001(bus,addr);
	}
	return 0;
}


#ifdef CONFIG_I2C_XCMD
int float_to_fx16(float f)
{
	unsigned e = 0;
	if (f >= 0) {
		do {
			if (f < Ranges[e]) {
				float lsb = (1 << e) * 0.01;
				return f/lsb;
			}
			++e;
		} while (e < sizeof(Ranges)/sizeof(Ranges[0]));
	}
	return -1;
}


const char *OPT3001::exeCmd(Terminal &term, int argc, const char **args)
{
	static const char *Modes[] = { "off", "single", "continuous", "continuous" };
	uint8_t data[2];
	if (i2c_w1rd(m_bus,m_addr,REG_CONFIG,data,sizeof(data)))
		return "I2C error.";
	uint16_t cfg = (data[0] << 8) | data[1];
	if ((cfg & MASK_CONFIG) != (m_cfg & MASK_CONFIG)) {
		log_warn(TAG,"unexpected change in config %02x => %02x",m_cfg,cfg);
		m_cfg = cfg;
	}
	if (0 == argc) {
		term.printf("config register: 0x%04x\n",cfg);
		term.printf("conversion time: %ums\n",cfg&BITS_CONVTIME?800:100);
		term.printf("mode %s\n",Modes[(cfg&BITS_MODE)>>SHIFT_MODE]);
		if (0xc000 == (cfg & BITS_RANGE))
			term.println("range auto");
		else
			term.printf("range %g\n",Ranges[(cfg&BITS_RANGE)>>SHIFT_RANGE]);
		term.printf("flags:%s%s%s%s\n"
				,cfg&BITS_OVERFLOW?" overflow":""
				,cfg&BITS_READY?" ready":""
				,cfg&BITS_FLAGHIGH?" high":""
				,cfg&BITS_FLAGLOW?" low":""
				);
		term.printf("latch %s\n",(cfg&BITS_LATCH)?"on":"off");
		term.printf("interrupts active %s\n",(cfg&BITS_POLARITY)?"high":"low");
		term.printf("exponent %smasked\n",(cfg&BITS_FAULTCNT)?"":"un");
		term.printf("fault count %u\n",1<<(cfg&BITS_FAULTCNT));
		if (i2c_w1rd(m_bus,m_addr,REG_LOWLIM,data,sizeof(data)))
			return "I2C error.";
		uint16_t low = (data[0] << 8) | data[1];
		term.printf("low limit %g (raw 0x%04x)\n",val2float(low),low);
		if (i2c_w1rd(m_bus,m_addr,REG_HIGHLIM,data,sizeof(data)))
			return "I2C error.";
		uint16_t high = (data[0] << 8) | data[1];
		term.printf("high limit %g (raw 0x%04x)\n",val2float(high),high);
	} else if (1 == argc) {
		if (0 == strcmp(args[0],"-h")) {
			term.println(
				"mode : set mode (o[ff],s[ingle],c[ontinuous])\n"
				"ct   : set conversion time\n"
				"low  : set low limit\n"
				"high : set high limit\n"
				"intr : set interrupt mode (hi/lo)\n"
				"fc   : fault count (1..4)\n"
				"range: set range (40..83865/auto)\n"
				"latch: set latch (on/off)\n"
				"mask : set mask exponent (on/off)\n"
				"exp  : set exponent\n"
			);
		} else if (0 == strcmp(args[0],"read")) {
			return read(true) < 0 ? "Failed." : 0;
		} else if (0 == strcmp(args[0],"save")) {
			char id[16];
			snprintf(id,sizeof(id),"opt3001@%u,%02x",m_bus,m_addr);
			nvm_store_u16(id,cfg);
			return 0;
		} else {
			return "Invalid argument #1.";
		}
	} else if (2 == argc) {
		if (0 == strcmp(args[0],"ct")) {
			long l = strtol(args[1],0,0);
			if (100 == l) {
				cfg &= ~BITS_CONVTIME;
			} else if (800 == l) {
				cfg |= BITS_CONVTIME;
			} else {
				return "Invalid argument #2.";
			}
		} else if (0 == strcmp(args[0],"mode")) {
			cfg &= ~BITS_MODE;
			if ('o' == args[1][0]) 
				cfg |= MODE_OFF;
			else if ('s' == args[1][0]) 
				cfg |= MODE_SINGLE;
			else if ('c' == args[1][0]) 
				cfg |= MODE_CONT;
			else
				return "Invalid argument #2.";
		} else if (0 == strcmp(args[0],"fc")) {
			long l = strtol(args[1],0,0);
			cfg &= ~BITS_FAULTCNT;
			if (1 == l) {
			} else if (2 == l) {
				cfg |= 1;
			} else if (4 == l) {
				cfg |= 2;
			} else if (8 == l) {
				cfg |= 3;
			} else {
				return "Invalid argument #2.";
			}
		} else if (0 == strcmp(args[0],"intr")) {
			if (0 == strncmp("hi",args[1],2)) {
				cfg |= BITS_POLARITY;
			} else if (0 == strncmp("lo",args[1],2)) {
				cfg &= ~BITS_POLARITY;
			} else {
				return "Invalid argument #2.";
			} 
		} else if (0 == strcmp(args[0],"latch")) {
			if (0 == strncmp("on",args[1],2)) {
				cfg |= BITS_LATCH;
			} else if (0 == strncmp("off",args[1],2)) {
				cfg &= ~BITS_LATCH;
			} else {
				return "Invalid argument #2.";
			} 
		} else if (0 == strcmp(args[0],"mask")) {
			if (0 == strncmp("on",args[1],2)) {
				cfg |= BITS_MASKEXP;
			} else if (0 == strncmp("off",args[1],2)) {
				cfg &= ~BITS_MASKEXP;
			} else {
				return "Invalid argument #2.";
			} 
		} else if (0 == strcmp(args[0],"exp")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if ((l < 0) || (l > 15))
				return "Invalid argument #2.";
			cfg &= BITS_EXP;
			cfg |= l << SHIFT_RANGE;
		} else if (0 == strcmp(args[0],"low")) {
			char *e;
			float f = strtof(args[1],&e);
			int v = float_to_fx16(f);
			if (v >= 0) {
				uint8_t out[] = { m_addr, REG_LOWLIM, (uint8_t)(v>>8), (uint8_t)(v&0xff) };
				if (i2c_write(m_bus,out,sizeof(out),1,1))
					return "I2C error.";
				return 0;
			} else {
				return "Invalid argument #2.";
			}
		} else if (0 == strcmp(args[0],"high")) {
			char *e;
			float f = strtof(args[1],&e);
			int v = float_to_fx16(f);
			if (v >= 0) {
				uint8_t out[] = { m_addr, REG_HIGHLIM, (uint8_t)(v>>8), (uint8_t)(v&0xff) };
				if (i2c_write(m_bus,out,sizeof(out),1,1))
					return "I2C error.";
				return 0;
			} else {
				return "Invalid argument #2.";
			}
		} else {
			return "Invalid argument #1.";
		}
		if (updateConfig(cfg))
			return "I2C error.";
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}
#endif


int OPT3001::init()
{
	log_dbug(TAG,"init");
	char id[16];
	snprintf(id,sizeof(id),"opt3001@%u,%02x",m_bus,m_addr);
	// default:
	// configure to contiuous sampling
	// 800ms sampling time
	uint16_t v = nvm_read_u16(id,m_cfg);
	if (updateConfig(v))
		return 1;
	return 0;
}


int OPT3001::read(bool force)
{
	uint8_t data[2] = { 0, 0 };
	if (!force) {
		if (int r = i2c_w1rd(m_bus,m_addr,REG_CONFIG,data,sizeof(data))) {
			log_warn(TAG,"read error: %s",esp_err_to_name(r));
			m_lum.set(NAN);
			return -r;
		}
		log_dbug(TAG,"flags:%s%s%s"
			, data[1]&BIT_FR?" CRF":""
			, data[1]&BIT_FH?" FH":""
			, data[1]&BIT_FL?" FL":""
			);
		if (0 == (data[1]&BIT_FR))
			return 50;
	}
	if (int r = i2c_w1rd(m_bus,m_addr,REG_RESULT,data,sizeof(data))) {
		log_warn(TAG,"read error: %s",esp_err_to_name(r));
		m_lum.set(NAN);
		return 5000;
	}
	log_dbug(TAG,"read: %02x %02x",data[0],data[1]);
	uint16_t val = ((uint16_t)data[0] << 8) | data[1];
	uint8_t exp = val >> 12;
	val &= 0xfff;
	float scale = ((float)(1<<exp))/100.0;
	float lum = (float)val * scale;
	m_lum.set(lum);
	return (m_cfg & BITS_CONVTIME) ? 800 : 100;
}


int OPT3001::updateConfig(uint16_t cfg)
{
	uint8_t out[] = { m_addr, REG_CONFIG, (uint8_t)(cfg>>8), (uint8_t)(cfg&0xff) };
	if (esp_err_t e = i2c_write(m_bus,out,sizeof(out),1,1)) {
		log_warn(TAG,"failed to update config");
		return e;
	}
	log_dbug(TAG,"updated config: 0x%04x",cfg);
	m_cfg = cfg;
	return 0;
}


void OPT3001::read(void *arg)
{
	OPT3001 *dev = (OPT3001 *) arg;
	dev->read();
}


void OPT3001::setMode(mode_t m)
{
	uint16_t cfg = m_cfg;
	cfg &= ~BITS_MODE;
	cfg |= m << SHIFT_MODE;
	updateConfig(cfg);
}


void OPT3001::single(void *arg)
{
	OPT3001 *dev = (OPT3001 *) arg;
	dev->setMode(mode_single);
}


void OPT3001::cont(void *arg)
{
	OPT3001 *dev = (OPT3001 *) arg;
	dev->setMode(mode_cont);
}


void OPT3001::stop(void *arg)
{
	OPT3001 *dev = (OPT3001 *) arg;
	dev->setMode(mode_off);
}


#endif // CONFIG_OPT3001
