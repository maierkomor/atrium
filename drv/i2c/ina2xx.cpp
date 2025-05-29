/*
 *  Copyright (C) 2018-2025, Thomas Maier-Komor
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

#ifdef CONFIG_INA2XX

#define TAG MODULE_INA2XX

#include "actions.h"
#include "cyclic.h"
#include "hwcfg.h"
#include "ina2xx.h"
#include "nvm.h"
#include "log.h"
#include "terminal.h"
#include "xio.h"

#include <esp_err.h>

/*
 * Calculation/Measurment:
 * - power is always calculated in the uC
 * - current is calculated in the uC if Ilsb (or Imax) is not set
 *
 * Configuration:
 * - to configure set the shunt value if it is not device internal
 * - for devices with alert pin, configure Ilsb or Imax to calibrate the
 *   device
 * - all settings are made persistent implicitly
 * - sampling can be performed cyclic by setting the cycle interval itv
 * - single samples can be obtained by executing the !sample action
 * - !sample actions read available data in continuous mode and trigger
 *   a conversion and poll for the result otherwise.
 *
 * Interrupt handling:
 * Devices that have an alert pin, can trigger an interrupt to
 * sample the data once the conversion is ready or to react on
 * an limit over-/underflow alert.
 * Adding the interrupt pin is done via I2C device config for the
 * relevant bus- and slave-id (driver type can be omitted).
 * Adding the interrupt does not impact sampling mode.
 * To use the interrupt, either bind the `intr event to the !read
 * action or to some alert action that is appropriate.
 *
 * Limitations:
 * - INA236 ADC range adjustment is not supported
 */

#define CALC_CURRENT	1

#define INA_REG_CONF	0x00
#define INA_REG_BUS	0x02
#define INA_REG_POW	0x03
#define INA_REG_AMP	0x04	// not ina260
#define INA_REG_CALIB	0x05	// not ina260
#define INA_REG_MASK	0x06
#define INA_REG_ALERT	0x07
#define INA_REG_MANID	0xfe	// ina226/ina260 only
#define INA_REG_DIEID	0xff	// ina226/ina260 only

#define INA_REG01	0x01
#define INA_REG02	0x02
#define INA_REG03	0x03
#define INA_REG04	0x04
#define INA_REG05	0x05
#define INA_REG06	0x06
#define INA_REG07	0x07

#define MODE_SMPL_CUR	0x1
#define MODE_SMPL_VLT	0x2
#define MODE_CONT	0x4

/* Reg 	INA219	INA220	INA226	INA236	INA260
 * 00h	cfg	cfg	cfg	cfg	cfg
 * 01h	shunt	shunt	shunt	shunt	cur
 * 02h	bus	bus	bus	bus	bus
 * 03h	pwr	pwr	pwr	pwr	pwr
 * 04h	cur	cur	cur	cur	---
 * 05h	cal	cal	cal	cal	---
 * 06h	---	---	mask	mask	mask
 * 07h	---	---	alert	alert	alert
 * vmax	26V	26V	36V	48V	36V
 * ADC	12bit	12bit	16bit	16bit	16bit
 * shnt	ext	ext	ext	ext	int
 * cal	0.04096	0.04096	0.00512	
 */

#define INA2XX_ADDR_MIN_ID	0x80
#define INA2XX_ADDR_MAX_ID	0x8f

#define CONF_RESET		(1 << 15) // bit to perform a reset
#define CONF_BRNG		(1 << 13)
#define CONF_BIT_PG		11
#define CONF_PG			(0x3 << CONF_BIT_PG)
#define CONF_BIT_BADC		7
#define CONF_BADC		(0xf << CONF_BIT_BADC)
#define CONF_BIT_SADC		3
#define CONF_SADC		(0xf << CONF_BIT_SADC)
#define CONF_BIT_MODE		0
#define CONF_MODE		(0x7 << CONF_BIT_MODE)
#define CONF_MODE_SHUNT		0x1
#define CONF_MODE_BUS		0x2
#define CONF_MODE_CONT		0x4
#define INA_CONF_RESET_VALUE	0x399f

#define SHIFT_ISHCT	3
#define SHIFT_VSHCT	3
#define SHIFT_VBUSCT	6
#define SHIFT_AVG	9
#define SHIFT_PG	11
#define SHIFT_BADC	7
#define SHIFT_SADC	3

#define MASK_ISHCT	0x0038
#define MASK_VSHCT	0x0038
#define MASK_VBUSCT	0x01c0
#define MASK_AVG	0x0e00
#define MASK_PG		0x1800
#define MASK_BADC	0x0f00
#define MASK_SADC	0x00f0

#define CFG_DFLT_219	0x399f
#define CFG_DFLT_220	0x399f
#define CFG_DFLT_226	0x4127
#define CFG_DFLT_260	0x6127

// INA219, INA220 only
#define BIT_BRNG	(1<<13)
#define BIT_BADC4	(1<<10)
#define BIT_SADC4	(1<<6)

// INA219, INA220, INA226 only
#define BIT_PG1		(1<<12)
#define BIT_PG0		(1<<11)

// mask register of INA226
#define BIT_SOL		(1<<15)	// shunt over-voltage
#define BIT_SUL		(1<<14)	// shunt under-voltage
#define BIT_BOL		(1<<13)	// bus over-voltage
#define BIT_BUL		(1<<12)	// bus under-voltage
#define BIT_POL		(1<<11)	// power over limit
#define BIT_CNVR	(1<<10)	// conversion ready
#define BIT_MEMERR	(1<<5)	// memory error
#define BIT_AFF		(1<<4)	// alert function flag
#define BIT_CVRF	(1<<3)	// conversion ready flag
#define BIT_OVF		(1<<2)	// math overflow
#define BIT_APOL	(1<<1)	// alert polarity 0=active-low
#define BIT_LATCHEN	(1<<0)	// alert latch enable
#define BITS_MODE	0xf800
#define BITS_ALERT	0xf800


struct cfg_ina219 {	// same for INA220
	uint16_t
		mode:3,
		sadc:4,		// shunt ADC resolution and sample count
		badc:4,		// bus ADC resolution and sample count
		pg:2,		// gain: 0:40mV,1:80mV,2:160mV,3:320mV
		brng:1,		// bus range: 0=16V, 1=32V
		reserved:1,
		rst:1;
};


union ucfg_ina219
{
	uint16_t cfgb;
	cfg_ina219 cfgs;
};


struct cfg_ina226 {
	uint16_t
		mode:3,
		vshct:3,	// shunt conversion time: 0.14ms..8.244ms
		vbusct:3,	// bus conversion time: 0.14ms..8.244ms
		avg:3,		// averaging count 
		adcr:1,		// INA236 only: shunt ADC range 1:+/-20.48mV, 0:+/-81.92mV
		reserved:2,
		rst:1;
};


union ucfg_ina226
{
	uint16_t cfgb;
	cfg_ina226 cfgs;
};


struct cfg_ina260 {
	uint16_t
		mode:3,
		ishct:3,	// shunt conversion time: 0.14ms..8.244ms
		vbusct:3,	// bus conversion time: 0.14ms..8.244ms
		avg:3,		// averaging count 
		reserved:3,
		rst:1;
};


union ucfg_ina260
{
	uint16_t cfgb;
	cfg_ina260 cfgs;
};


// also used for INA220
struct INA219 : public INA2XX
{
	INA219(uint8_t bus, uint8_t addr, bool is220)
	: INA2XX(bus,addr,ID_INA219+is220,is220?"ina220":"ina219")
	, m_shunt("shunt","mV","%4.3f")
	, m_is220(is220)
	{
		m_Blsb = 0.004;
		m_Slsb = is220 ? 10E-6 : 2.5E-6;
	}

	const char *drvName() const override
	{ return m_is220?"ina220":"ina219"; }

	void attach(class EnvObject *) override;
	float readBusV() override;

	protected:
	void init() override;
	void read() override;
	const char *setNumSamples(unsigned n) override;
	bool convReady() override;
	void writeConfig() override;

	EnvNumber m_shunt;
	bool m_is220;
};


// also used for INA236
struct INA226 : public INA2XX
{
	INA226(uint8_t bus, uint8_t addr, bool is236)
	: INA2XX(bus,addr,ID_INA226,is236?"ina236":"ina226")
	, m_shunt("shunt","mV","%4.3f")
	, m_is236(is236)
	{
		m_Blsb = is236 ? 1.6E-3 : 1.25E-3;
		m_Slsb = 2.5E-6;	// INA236 can switch ADC range to 625E-9;
	}

	const char *drvName() const override
	{ return m_is236?"ina236":"ina226"; }

	void addIntr(uint8_t gpio) override;
	void attach(class EnvObject *) override;

	protected:
	void init() override;
	void read() override;
	const char *setNumSamples(unsigned n) override;
	void writeConfig() override;
	const char *setAlert(uint16_t a, uint16_t l) override;
	const char *setCNVR(bool) override;

	EnvNumber m_shunt;
	uint16_t m_mask = 0, m_limit = 0;
	event_t m_isrev = 0, m_alev = 0;
	bool m_is236;
};


struct INA260 : public INA2XX
{
	INA260(uint8_t bus, uint8_t addr)
	: INA2XX(bus,addr,ID_INA260,"ina260")
	{
		m_res = 0.002;
	}

	const char *drvName() const override
	{ return "ina260"; }

	void addIntr(uint8_t gpio) override;
	void attach(class EnvObject *) override;
	float readCurrent() override;

	protected:
	void init() override;
	void read() override;
	const char *setNumSamples(unsigned n) override;
	const char *setAlert(uint16_t a, uint16_t l) override;
	const char *setCNVR(bool) override;
	const char *setShunt(float r) override;
	void writeConfig() override;

	uint16_t m_mask = 0, m_limit = 0;
	event_t m_isrev = 0, m_alev = 0;
};


static uint16_t ConvTime[] = {140,204,332,588,1100,2116,4156,8244};
static uint16_t Avgs[] = {1,4,16,64,128,256,512,1024};


static int conv_time_index(unsigned t)
{
	if ((t > 10000) || (t < 70))
		return -1;
	int idx = 0;
	do {
		uint16_t d = ConvTime[idx+1] - ConvTime[idx];
		d >>= 1;
		if (t <= ConvTime[idx]+d)
			return idx;
		++idx;
	} while (idx+1 < sizeof(ConvTime)/sizeof(ConvTime[0]));
	return idx;
}


static event_t init_alert(const char *name, uint8_t gpio)
{
	event_t ev = event_register(name,"`intr");
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = xio_cfg_pull_up;
	cfg.cfg_intr = xio_cfg_intr_fall;
	if (0 > xio_config(gpio,cfg)) {
		log_warn(TAG,"gpio %u config failed",gpio);
	} else if (xio_set_intr(gpio,event_isr_handler,(void*)(unsigned)ev)) {
		log_warn(TAG,"gpio%u interrupt failed",gpio);
	} else {
		log_info(TAG,"gpio%u triggers %s`intr",gpio,name);
		return ev;
	}
	return 0;
}


static float nvm_get_res(uint8_t type, uint8_t bus, uint8_t addr)
{
	char nvsn[32];
	sprintf(nvsn,"ina2%02u@%u,%x.r",type,bus,addr);
	return nvm_read_float(nvsn,0);
}


static void read_config(uint8_t type, uint8_t bus, uint8_t addr, INA2xxConfig &cfg)
{
	char nvsn[32];
	sprintf(nvsn,"ina2%02u@%u,%x.cfg",type,bus,addr>>1);
	uint8_t buf[INA2xxConfig::getMaxSize()];
	size_t s = sizeof(buf);
	if (0 == nvm_read_blob(nvsn,buf,&s)) {
		cfg.fromMemory(buf,s);
	}
}


static void write_config(uint8_t type, uint8_t bus, uint8_t addr, INA2xxConfig &cfg)
{
	char nvsn[32];
	sprintf(nvsn,"ina2%02u@%u,%x.cfg",type,bus,addr>>1);
	size_t s = cfg.calcSize();
	uint8_t buf[s];
	cfg.toMemory(buf,s);
	nvm_store_blob(nvsn,buf,s);
}


INA2XX::INA2XX(uint8_t bus, uint8_t addr, uint8_t type, const char *name)
: I2CDevice(bus,addr,name)
, m_volt("bus","V","%4.1f")
, m_amp("current","A","%4.4f")
, m_power("power","W","%4.1f")
, m_st(st_cont)
, m_type(type)
{
	if (const char *e = writeReg(INA_REG_CONF,CONF_RESET))
		log_warn(TAG,"reset failed: %s",e);
	if (const char *e = readReg(INA_REG_CONF,m_conf))
		log_warn(TAG,"read conf register failed: %s",e);
}


void INA226::addIntr(uint8_t gpio)
{
	m_isrev = init_alert(m_name,gpio);
	if (0 == m_isrev) {
		log_warn(TAG,"failed to init ISR event for gpio");
		return;
	}
}


void INA260::addIntr(uint8_t gpio)
{
	m_isrev = init_alert(m_name,gpio);
}


void INA2XX::attach(class EnvObject *root)
{
	root->add(&m_amp);
	root->add(&m_volt);
	root->add(&m_power);
}


void INA219::attach(class EnvObject *root)
{
	init();
	root->add(&m_shunt);
	INA2XX::attach(root);
}


void INA226::attach(class EnvObject *root)
{
	init();
	root->add(&m_shunt);
	INA2XX::attach(root);
}


void INA260::attach(class EnvObject *root)
{
	init();
	INA2XX::attach(root);
}


INA2XX *INA2XX::create(uint8_t bus, uint8_t addr, uint8_t type)
{
	// address is in 8bit format (already shifted to left for r/w)
	if ((type == ID_INA219)||(type == ID_INA220)) {
		log_info(TAG,"checking for INA%u at %d/0x%x",type,bus,addr);
		esp_err_t err;
		uint8_t v[2];
		if (addr) {
			err = i2c_w1rd(bus,addr,INA_REG_CONF,v,sizeof(v));
		} else {
			for (addr = 0x40<<1; addr < 0x50<<1; addr+=2) {
				err = i2c_w1rd(bus,addr,INA_REG_CONF,v,sizeof(v));
				if (err == 0)
					break;
			}
		}
		if (err)
			return 0;
	}
	switch (type) {
	case ID_INA219:
		return new INA219(bus,addr,false);
	case ID_INA220:
		return new INA219(bus,addr,true);
	case ID_INA226:
		return new INA226(bus,addr,false);
	case ID_INA236:
		return new INA226(bus,addr,true);
	case ID_INA260:
		return new INA260(bus,addr);
	default:
		log_warn(TAG,"invalid type id %u",type);
	}
	return 0;
}


void INA2XX::init()
{
}


void INA219::init()
{
	reset();
	m_acrd = action_add(concat(m_name,"!read"),INA2XX::read,static_cast<INA2XX *>(this),0);
	INA2xxConfig cfg;
	read_config(m_type,m_bus,m_addr,cfg);
	if (!cfg.has_res()) {
		uint16_t res = nvm_get_res(m_type,m_bus,m_addr);
		if (0 != res) {
			cfg.set_res(res);
		}
	}
	m_res = cfg.res();
	log_info(TAG,"restore shunt %4.3f",m_res);
	if (cfg.has_Ilsb()) {
		m_Ilsb = cfg.Ilsb();
		log_info(TAG,"restore Ilsb %4.3f",m_Ilsb);
	}
	if ((0 != m_Ilsb) && (0 != m_res))
		updateCal();
	if (cfg.has_interval()) {
		m_itv = cfg.interval();
		log_info(TAG,"restore interval %u",m_itv);
	}
	if (cfg.has_config()) {
		uint16_t conf = cfg.config();
		log_info(TAG,"restore config 0x%x",conf);
		writeReg(INA_REG_CONF,conf);
	}
}


void INA226::init()
{
	reset();
	m_acrd = action_add(concat(m_name,"!read"),INA2XX::read,static_cast<INA2XX *>(this),0);
	m_alev = event_register(m_name,"`alarm");
	INA2xxConfig cfg;
	read_config(m_type,m_bus,m_addr,cfg);
	if (!cfg.has_res()) {
		uint16_t res = nvm_get_res(m_type,m_bus,m_addr);
		if (0 != res) {
			cfg.set_res(res);
		}
	}
	m_res = cfg.res();
	log_info(TAG,"restore shunt %4.3f",m_res);
	if (cfg.has_Ilsb()) {
		m_Ilsb = cfg.Ilsb();
		log_info(TAG,"restore Ilsb %4.3f",m_Ilsb);
	}
	if ((0 != m_Ilsb) && (0 != m_res))
		updateCal();
	if (cfg.has_config()) {
		uint16_t conf = cfg.config();
		log_info(TAG,"restore config 0x%x",conf);
		writeReg(INA_REG_CONF,conf);
		m_conf = conf;
	}
	if (cfg.has_interval()) {
		m_itv = cfg.interval();
		log_info(TAG,"restore interval %u",m_itv);
	}
	if (cfg.has_limit()) {
		uint16_t limit = cfg.limit();
		log_info(TAG,"restore limit %u",limit);
		writeReg(INA_REG_ALERT,limit);
		m_limit = limit;
	}
	if (cfg.has_mask()) {
		uint16_t mask = cfg.mask();
		log_info(TAG,"restore mask 0x%x",mask);
		writeReg(INA_REG_MASK,mask);
		m_mask = mask;
	} else {
		readReg(INA_REG_MASK,m_mask);
	}
	if (m_mask & BIT_CNVR)
		event_callback(m_isrev,m_acrd);
	else
		cyclic_add_task(m_name,cyclic,(INA2XX *)this,0);
}


void INA260::init()
{
	reset();
	action_add(concat(m_name,"!read"),INA2XX::read,static_cast<INA2XX *>(this),0);
	m_alev = event_register(m_name,"`alarm");
	INA2xxConfig cfg;
	read_config(m_type,m_bus,m_addr,cfg);
	if (cfg.has_Ilsb()) {
		m_Ilsb = cfg.Ilsb();
		log_info(TAG,"restore Ilsb %4.3f",m_Ilsb);
	}
	if ((0 != m_Ilsb) && (0 != m_res))
		updateCal();
	if (cfg.has_config()) {
		uint16_t conf = cfg.config();
		log_info(TAG,"restore config 0x%x",conf);
		writeReg(INA_REG_CONF,conf);
		m_conf = conf;
	}
	if (cfg.has_interval()) {
		m_itv = cfg.interval();
		log_info(TAG,"restore interval %u",m_itv);
	}
	if (cfg.has_limit()) {
		uint16_t limit = cfg.limit();
		log_info(TAG,"restore limit %u",limit);
		writeReg(INA_REG_ALERT,limit);
	}
	if (cfg.has_mask()) {
		uint16_t mask = cfg.mask();
		if (m_isrev) {
			mask |= BIT_CVRF;
			log_info(TAG,"restore mask 0x%x with interrupts enabled",mask);
		} else {
			log_info(TAG,"restore mask 0x%x",mask);
		}
		writeReg(INA_REG_MASK,mask);
	}
}


void INA2XX::updateCal()
{
	float cal;
	switch (m_type) {
	case ID_INA219:
	case ID_INA220:
		cal = 0.04096;
		break;
	case ID_INA226:
	case ID_INA236:
		cal = 0.00512;
		break;
	case ID_INA260:
		// no calibration register, not necessary
		return;
	default:
		log_warn(TAG,"invalid type id %u",m_type);
		return;
	}
	cal = truncf(cal / (m_Ilsb * m_res));
	uint16_t calv = cal;
	if (const char *e = writeReg(INA_REG_CALIB,calv))
		log_warn(TAG,"write calibration failed: %s",e);
	else
		log_info(TAG,"calibration %u",(unsigned)calv);
}


const char *INA2XX::writeReg(uint8_t reg, uint16_t val)
{
	if (esp_err_t e = i2c_write3(m_bus,m_addr,reg,(uint8_t)(val>>8),(uint8_t)(val&0xff)))
		return esp_err_to_name(e);
	return 0;
}


const char *INA2XX::readReg(uint8_t reg, uint16_t &val)
{
	uint8_t regs[2];
	if (esp_err_t err = i2c_w1rd(m_bus,m_addr,reg,regs,sizeof(regs)))
		return esp_err_to_name(err);
	val = (regs[0]<<8)|regs[1];
	return 0;
}


void INA2XX::writeConfig()
{
	log_warn(TAG,"persistent storage not supported for %s",drvName());
}


void INA219::writeConfig()
{
	INA2xxConfig cfg;
	cfg.set_config(m_conf);
	cfg.set_res(m_res);
	cfg.set_Ilsb(m_Ilsb);
	cfg.set_interval(m_itv);
	write_config(m_type,m_bus,m_addr,cfg);
}


void INA226::writeConfig()
{
	INA2xxConfig cfg;
	cfg.set_config(m_conf);
	cfg.set_res(m_res);
	cfg.set_mask(m_mask);
	cfg.set_Ilsb(m_Ilsb);
	cfg.set_limit(m_limit);
	cfg.set_interval(m_itv);
	write_config(m_type,m_bus,m_addr,cfg);
}


void INA260::writeConfig()
{
	INA2xxConfig cfg;
	cfg.set_config(m_conf);
	cfg.set_mask(m_mask);
	cfg.set_Ilsb(m_Ilsb);
	cfg.set_limit(m_limit);
	cfg.set_interval(m_itv);
	write_config(m_type,m_bus,m_addr,cfg);
}


void INA2XX::setConfig(uint16_t cfgv)
{
	if (const char *e = writeReg(INA_REG_CONF,cfgv)) {
		log_warn(TAG,"set config failed: %s",e);
	} else {
		log_info(TAG,"new config 0x%x",cfgv);
		m_conf = cfgv;
		writeConfig();
	}
}


int INA2XX::setMode(bool cur, bool volt, bool cont)
{
	uint16_t cfg = m_conf;
	if (cur)
		cfg |= MODE_SMPL_CUR;
	else
		cfg &= ~MODE_SMPL_CUR;
	if (volt)
		cfg |= MODE_SMPL_VLT;
	else
		cfg &= !MODE_SMPL_VLT;
	if (cont)
		cfg |= MODE_CONT;
	else
		cfg &= ~MODE_CONT;
	setConfig(cfg);
	return 0;
}


const char *INA2XX::setAlert(uint16_t a, uint16_t l)
{
	return "Opertion not supported.";
}


const char *INA226::setAlert(uint16_t a, uint16_t l)
{
	uint16_t reg = 0;
	if (const char *e = readReg(INA_REG_MASK,reg))
		return e;
	reg &= ~BITS_ALERT;
	reg |= a;
	if (a)
		reg |= BIT_LATCHEN;	// required for alerts
	else
		reg &= ~BIT_LATCHEN;
	if (const char *e = writeReg(INA_REG_MASK,reg))
		return e;
	m_mask = reg;
	if (const char *e = writeReg(INA_REG_ALERT,l))
		return e;
	m_limit = l;
	writeConfig();
	return 0;
}


const char *INA260::setAlert(uint16_t a, uint16_t l)
{
	uint16_t reg = 0;
	if (const char *e = readReg(INA_REG_MASK,reg))
		return e;
	reg &= ~BITS_ALERT;
	reg |= a;
	if (a)
		reg |= BIT_LATCHEN;	// required for alerts
	else
		reg &= ~BIT_LATCHEN;
	if (const char *e = writeReg(INA_REG_MASK,reg))
		return e;
	m_mask = reg;
	if (const char *e = writeReg(INA_REG_ALERT,l))
		return e;
	m_limit = l;
	writeConfig();
	return 0;
}


const char *INA2XX::setCNVR(bool en)
{
	return "Operation not supported.";
}


const char *INA226::setCNVR(bool en)
{
	uint16_t mask;
	if (const char *e = readReg(INA_REG_MASK,mask))
		return e;
	if (en) {
		mask |= BIT_CNVR;
		cyclic_rm_task(m_name);
		event_callback(m_isrev,m_acrd);
	} else {
		mask &= ~BIT_CNVR;
		event_detach(m_isrev,m_acrd);
		cyclic_add_task(m_name,cyclic,(INA2XX *)this,0);
	}
	if (const char *e = writeReg(INA_REG_MASK,mask))
		return e;
	m_mask = mask;
	writeConfig();
	return 0;
}


const char *INA260::setCNVR(bool en)
{
	uint16_t mask;
	if (const char *e = readReg(INA_REG_MASK,mask))
		return e;
	if ((en ^ ((mask & BIT_CNVR) != 0)) == 0)
		return 0;
	if (en) {
		mask |= BIT_CNVR;
		cyclic_rm_task(m_name);
		event_callback(m_isrev,m_acrd);
	} else {
		mask &= ~BIT_CNVR;
		event_detach(m_isrev,m_acrd);
		cyclic_add_task(m_name,cyclic,(INA2XX *)this,0);
	}
	if (const char *e = writeReg(INA_REG_MASK,mask))
		return e;
	m_mask = mask;
	writeConfig();
	return 0;
}


const char *INA2XX::setShunt(float r)
{
	m_res = r;
	updateCal();
	writeConfig();
	return 0;
}


const char *INA260::setShunt(float r)
{
	return "Operation not supported.";
}


unsigned INA2XX::cyclic(void *arg)
{
	INA2XX *dev = (INA2XX *)arg;
	return dev->vcyclic();
}


unsigned INA2XX::vcyclic()
{
	switch (m_st) {
	case st_read:
		m_st = st_off;
		m_conf &= ~CONF_MODE;
		/* FALLTHRU */
	case st_cont:
		read();
		break;
	case st_trigger:
		{
			uint16_t v = (m_conf & ~CONF_MODE) | CONF_MODE_BUS | CONF_MODE_SHUNT;
			if (0 == i2c_write3(m_bus,m_addr,INA_REG_CONF,(uint8_t)(v>>8),(uint8_t)v)) {
				m_st = st_read;
				m_conf = v;
			}
		}
		break;
	case st_off:
		break;
	default:
		abort();
	}
	return m_itv;
}


void INA2XX::read(void *arg)
{
	INA2XX *dev = (INA2XX *) arg;
	dev->read();
}


void INA2XX::read()
{
	log_warn(TAG,"unimplemented read");
}


void INA219::read()
{
	float fshnt = NAN, fvlt = NAN;
	int16_t shnt = 0;
	uint16_t vlt = 0;
	uint8_t data[2];
	if (0 == i2c_w1rd(m_bus,m_addr,INA_REG01,data,sizeof(data))) {
		shnt = (int16_t) ((data[0] << 8) | data[1]);
		fshnt = (float)shnt * m_Slsb;
	}
	if (0 == i2c_w1rd(m_bus,m_addr,INA_REG_BUS,data,sizeof(data))) {
		vlt = (uint16_t) ((data[0] << 8) | data[1]);
		vlt >>= 3;
		fvlt = (float)vlt * m_Blsb;
	}
	m_shunt.set(fshnt);
	m_volt.set(fvlt);
	float fcur = fshnt / m_res * 1E-3;
	m_amp.set(fcur);
	float fpwr = fcur*fvlt;
	if (fpwr < 0)
		fpwr *= -1;
	m_power.set(fpwr);
	//log_dbug(TAG,"shnt %4.3fmV, cur %4.3fA, vlt %4.3fV, pwr %4.3fW, res %4.3f",fshnt,fcur,fvlt,fpwr,m_res);
}


void INA226::read()
{
	uint16_t mask;
	bool err = false, cvr = false, aff = false;;
	if (0 == readReg(INA_REG_MASK,mask)) {
		if (mask & BIT_AFF) {
			event_trigger(m_alev);
			aff = true;
			log_dbug(TAG,"AFF");
		}
		cvr |= ((mask & BIT_CVRF) != 0);
		err = ((mask & BIT_OVF) != 0) | ((mask & BIT_MEMERR) != 0);
	}
	float fshnt = NAN, fvlt = NAN, fpwr = NAN, fcur = NAN;
	if (err) {
		log_info(TAG,"overflow/memory error");
	} else if (cvr) {
		uint16_t shnt = 0;
		if (0 == readReg(INA_REG01,shnt)) {
			fshnt = (float)(int16_t)shnt * m_Slsb;	// 2.5E-6 factor, but converted to mV for m_shunt
			if (aff)
				log_dbug(TAG,"AFF! shunt %u => %4.3f",shnt,fshnt);
		}
		uint16_t vbus = 0;
		if (0 == readReg(INA_REG_BUS,vbus))
			fvlt = (float)vbus * m_Blsb;
#ifdef CALC_CURRENT
		fcur = fshnt / m_res;
#else
		uint16_t cur;
		if (0 == readReg(INA_REG_AMP,cur))
			fcur = (float)(int16_t)cur * m_Ilsb;
#endif
		fpwr = fvlt * fcur;
		if (fpwr < 0)
			fpwr *= -1;
	}
	m_shunt.set(fshnt*1E3);	// convert to mV
	m_volt.set(fvlt);
	m_amp.set(fcur);
	m_power.set(fpwr);
	//log_dbug(TAG,"read shnt %4.3fV, cur %4.3fA, vlt %4.3fV, pwr %4.3fW",fshnt,fcur,fvlt,fpwr);
}


void INA260::read()
{
	uint16_t mask;
	bool err = false, cvr = false, aff = false;;
	if (0 == readReg(INA_REG_MASK,mask)) {
		if (mask & BIT_AFF) {
			event_trigger(m_alev);
			aff = true;
			log_dbug(TAG,"AFF");
		}
		cvr |= ((mask & BIT_CVRF) != 0);
		err = ((mask & BIT_OVF) != 0) | ((mask & BIT_MEMERR) != 0);
	}
	float fvlt = NAN, fpwr = NAN, fcur = NAN;
	if (err) {
		log_info(TAG,"overflow/memory error");
	} else if (cvr) {
		uint16_t vbus = 0;
		if (0 == readReg(INA_REG_BUS,vbus))
			fvlt = (float)vbus * m_Blsb;
		uint16_t cur;
		if (0 == readReg(INA_REG01,cur)) {
			fcur = (float)(int16_t)cur * m_Ilsb;
			if (aff)
				log_dbug(TAG,"AFF! current %u => %4.3f",cur,fcur);
		}
		fpwr = fvlt * fcur;
		if (fpwr < 0)
			fpwr *= -1;
	}
	m_volt.set(fvlt);
	m_amp.set(fcur);
	m_power.set(fpwr);
	//log_dbug(TAG,"read shnt %4.3fV, cur %4.3fA, vlt %4.3fV, pwr %4.3fW",fshnt,fcur,fvlt,fpwr);
}


float INA2XX::readBusV()
{
	uint16_t bus;
	if (0 == readReg(INA_REG_BUS,bus)) {
		return (bus & 0x7fff) * m_Blsb;
	}
	return NAN;
}


float INA219::readBusV()
{
	uint16_t bus;
	if (0 == readReg(INA_REG_BUS,bus)) {
		return (bus>>3) * m_Blsb;
	}
	return NAN;
}


float INA2XX::readCurrent()
{
	uint16_t cur;
	if (0 == readReg(INA_REG04,cur)) {
		return (int16_t) cur * m_Ilsb;
	}
	return NAN;
}


float INA260::readCurrent()
{
	uint16_t cur;
	if (0 == readReg(INA_REG01,cur)) {
		return (int16_t) cur * 1.25E-3;
	}
	return NAN;
}


void INA2XX::sample(void *arg)
{
	INA2XX *dev = (INA2XX *) arg;
	dev->sample();
}


bool INA2XX::convReady()
{
	uint16_t mask;
	if (readReg(INA_REG_MASK,mask))
		return false;
	return (mask & BIT_CVRF) != 0;
}


bool INA219::convReady()
{
	uint16_t bus;
	if (readReg(INA_REG_BUS,bus))
		return false;
	return (bus & 2) != 0;
}


void INA2XX::sample()
{
	if (m_conf & MODE_CONT) {
		if (m_conf & MODE_SMPL_VLT) {
			m_volt.set(readBusV());
		}
		if (m_conf & MODE_SMPL_CUR) {
			m_amp.set(readCurrent());
		}
	} else {
		uint16_t conf;
		if (readReg(INA_REG_CONF,conf))
			return;
		conf |= CONF_MODE_SHUNT | CONF_MODE_BUS;
		if (writeReg(INA_REG_CONF,conf))
			return;
		while (!convReady());
		m_volt.set(readBusV());
		m_amp.set(readCurrent());
	}
}


static void printMode(Terminal &term, uint16_t v)
{
	if (v & (MODE_SMPL_CUR|MODE_SMPL_VLT)) 
		term.printf("mode %s%s%s\n"
			,v&MODE_CONT?"continuous":"triggered"
			,v&MODE_SMPL_CUR?" shunt":""
			,v&MODE_SMPL_VLT?" bus":""
		);
	else
		term.println("mode power-down");
}


static void printConfig_219(Terminal &term, uint16_t v)
{
	term.printf(
		"BRNG %uV\n"
		"PG   %u0mV\nBADC "
		,v&BIT_BRNG?32:16
		,4 << ((v&MASK_PG)>>SHIFT_PG)
	);
	if (v&BIT_BADC4)
		term.printf("%ubit\n",((v>>SHIFT_BADC)&7)+9);
	else
		term.printf("%usamples\n",1<<((v>>SHIFT_BADC)&7));
	if (v&BIT_SADC4)
		term.printf("SADC %ubit\n",((v>>SHIFT_SADC)&7)+9);
	else
		term.printf("SADC %usamples\n",1<<((v>>SHIFT_SADC)&7));
}


static void printConfig_AVI(Terminal &term, uint16_t v)
{
	term.printf(
		"AVG %u\n"
		"VCT %uus\n"
		"ICT %uus\n"
		,Avgs[(v&MASK_AVG)>>SHIFT_AVG]
		,ConvTime[(v&MASK_VBUSCT)>>SHIFT_VBUSCT]
		,ConvTime[(v&MASK_VSHCT)>>SHIFT_VSHCT]
	);
}


const char *INA2XX::setNumSamples(unsigned n)
{
	return "Operation not supported.";
}


const char *INA219::setNumSamples(unsigned n)
{
	unsigned avg = 0;
	while ((n&1) == 0) {
		n >>= 1;
		++avg;
	}
	if (n&~1)
		return "Invalid sample count.";
	avg |= 0x8;
	uint16_t cfg = m_conf;
	cfg &= ~MASK_SADC&~MASK_BADC;
	cfg |= avg << SHIFT_BADC;
	cfg |= avg << SHIFT_SADC;
	setConfig(cfg);
	log_dbug(TAG,"AVG set to %u\n",avg+1);
	return 0;
}


const char *INA226::setNumSamples(unsigned n)
{
	unsigned avg = 0;
	while ((avg < sizeof(Avgs)/sizeof(Avgs[0])) && (n != Avgs[avg])) {
		++avg;
	}
	if (avg == sizeof(Avgs)/sizeof(Avgs[0]))
		return "Invalid sample count.";
	uint16_t cfg = m_conf;
	cfg &= ~MASK_AVG;
	cfg |= avg << SHIFT_AVG;
	setConfig(cfg);
	log_dbug(TAG,"AVG set to %u\n",avg+1);
	return 0;
}


const char *INA260::setNumSamples(unsigned n)
{
	unsigned avg = 0;
	while ((avg < sizeof(Avgs)/sizeof(Avgs[0])) && (n != Avgs[avg])) {
		++avg;
	}
	if (avg == sizeof(Avgs)/sizeof(Avgs[0]))
		return "Invalid sample count.";
	uint16_t cfg = m_conf;
	cfg &= ~MASK_AVG;
	cfg |= avg << SHIFT_AVG;
	setConfig(cfg);
	log_dbug(TAG,"AVG set to %u\n",avg+1);
	return 0;
}


esp_err_t INA2XX::reset()
{
	esp_err_t err = i2c_write3(m_bus,m_addr,INA_REG_CONF,0x80,0x00);
	if (0 == err) {
		if (const char *e = readReg(INA_REG_CONF,m_conf))
			log_warn(TAG,"config register not readable after reset: %s",e);
		else
			log_info(TAG,"reset device at %d,0x%x: config %04x",m_bus,m_addr,m_conf);
	}
	return err;
}


#ifdef CONFIG_I2C_XCMD
const char *INA2XX::exeCmd(Terminal &term, int argc, const char **args)
{
	if ((argc == 0) || (0 == strcmp(args[0],"-h"))) {
		term.println(
			"conf <v> : get/set configuration register\n"
			"itv <i>  : set cyclic sampling interval (0 to disable)\n"
			"brng <v> : set bus voltage range (valid values: 16, 32)\n"
			"pg <r>   : set shunt range (valid values: 40, 80, 160, 320)\n"
			"avg <c>  : set number of samples for averaging\n"
			"mode <m> : set mode (off, bus, shunt, both, bus1, shunt1, both1)\n"
			"reset    : reset to power-on defaults\n"
			"shunt <r>: set shunt resistor to <r> Ohm\n"
			"vct <t>  : set bus voltage conversion time\n"
			"ict <t>  : set current conversion time\n"
			"           <t> in {140,204,332,588,1100,2116,4156,8244}\n"
			"cnvr <b> : en-/disable read on conversion ready interrupt\n"
			"pol <l>  : set power over limit\n"
			"sol <l>  : set shunt over limit\n"
			"sul <l>  : set shunt under limit\n"
			"bol <l>  : set bus over limit\n"
			"bul <l>  : set bus under limit\n"
			"Imax <c> : set maximum current (used for calibration)\n"
			"Ilsb <c> : set current LSB (set either Imax or Ilsb)\n"
			"nolimit  : disable alerts\n"
			);
		return 0;
	}
	uint16_t v;
	readReg(INA_REG_CONF,v);
	if (argc == 1) {
		if (v != m_conf) {
			log_warn(TAG,"hidden conf update: %x => %x",m_conf,v);
			m_conf = v;
		}
		if (0 == strcmp(args[0],"conf")) {
			printMode(term,v);
			if ((ID_INA260 == m_type) || (ID_INA226 == m_type) || (ID_INA236 == m_type))
				printConfig_AVI(term,v);
			else if ((ID_INA219 == m_type) || (ID_INA220 == m_type))
				printConfig_219(term,v);
		} else if (0 == strcmp(args[0],"brng")) {
			if ((ID_INA219 == m_type) || (ID_INA220 == m_type))
				term.printf("brng %d\n",v & CONF_BRNG ? 32 : 16);
			else
				return "Operation not supported.";
		} else if (0 == strcmp(args[0],"pg")) {
			if ((ID_INA260 == m_type) || (ID_INA226 == m_type))
				term.printf("gain /%d\n", 1 << ((v & CONF_PG) >> CONF_BIT_PG));
			else
				return "Operation not supported.";
		} else if (0 == strcmp(args[0],"mode")) {
			printMode(term,v);
		} else if (0 == strcmp(args[0],"shunt")) {
			term.printf("shunt %4.3f Ohm\n",m_res);
		} else if (0 == strcmp(args[0],"Imax")) {
			term.printf("Imax %4.3f A\n",m_Ilsb * (1<<15));
		} else if (0 == strcmp(args[0],"Ilsb")) {
			term.printf("Ilsb %4.5f A\n",m_Ilsb);
		} else if (0 == strcmp(args[0],"itv")) {
			term.printf("itv %u\n",m_itv);
		} else if (0 == strcmp(args[0],"reset")) {
			reset();
		} else if (0 == strcmp(args[0],"limit")) {
			uint16_t limit;
			if (const char *e = readReg(INA_REG_ALERT,limit))
				return e;
			term.printf("limit %u\n",(uint32_t)limit);
		} else if (0 == strcmp(args[0],"nolimit")) {
			uint16_t mask;
			if (const char *e = readReg(INA_REG_MASK,mask))
				return e;
			mask &= ~BITS_ALERT;
			if (const char *e = writeReg(INA_REG_MASK,mask))
				return e;
		} else if (0 == strcmp(args[0],"regs")) {
			for (uint8_t r = 0; r < 8; ++r) {
				uint16_t reg;
				if (0 == readReg(r,reg))
					term.printf("reg%u: 0x%04x (dec %d)\n",r,reg,reg);
			}
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 2) {
		char *e;
		long l = strtol(args[1],&e,0);
		if (0 == strcmp(args[0],"brng")) {
			if ((m_type != ID_INA219) && (m_type != ID_INA220))
				return "Operation not supported.";
			if (l == 16) {
				v &= ~CONF_BRNG;
			} else if (l == 32) {
				v |= CONF_BRNG;
			} else {
				return "Invalid argument #2.";
			}
			setConfig(v);
		} else if (0 == strcmp(args[0],"conf")) {
			if ((*e) || (l < 0) || (l > UINT16_MAX))
				return "Invalid argument #2.";
			v = l;
			setConfig(v);
		} else if (0 == strcmp(args[0],"itv")) {
			if ((*e) || (l < 0) || (l > UINT16_MAX))
				return "Invalid argument #2.";
			if ((0 != m_itv) && (0 == l)) {
				cyclic_rm_task(m_name);
			} else if ((0 == m_itv) && (0 != l)) {
				cyclic_add_task(m_name,cyclic,(INA2XX *)this,0);
			}
			m_itv = l;
			writeConfig();
		} else if (0 == strcmp(args[0],"Imax")) {
			float f = strtof(args[1],&e);
			if ((*e) || (f <= 0) || (f > 100))
				return "Invalid argument #2.";
			m_Ilsb = f / (float)(1<<15);
			updateCal();
			writeConfig();
		} else if (0 == strcmp(args[0],"Ilsb")) {
			float f = strtof(args[1],&e);
			if ((*e) || (f <= 0) || (f > 1))
				return "Invalid argument #2.";
			m_Ilsb = f;
			updateCal();
			writeConfig();
		} else if (0 == strcmp(args[0],"pg")) {
			if ((m_type != ID_INA219) && (m_type != ID_INA220))
				return "Operation not supported.";
			v &= ~CONF_PG;
			if ((l == 1) || (l == 40)) {
				//v |= (0 << CONF_BIT_PG);
			} else if ((l == 2) || (l == 80)) {
				v |= (1 << CONF_BIT_PG);
			} else if ((l == 4) || (l == 160)) {
				v |= (2 << CONF_BIT_PG);
			} else if ((l == 8) || (l == 320)) {
				v |= (3 << CONF_BIT_PG);
			} else {
				return "Invalid argument #2.";
			}
			setConfig(v);
		} else if (0 == strcmp(args[0],"vct")) {
			if ((m_type != ID_INA226) && (m_type != ID_INA236) && (m_type != ID_INA260))
				return "Operation not supported.";
			int idx = conv_time_index(l);
			if (idx < 0)
				return "Invalid argument #2.";
			v &= ~MASK_VBUSCT;
			v |= idx << SHIFT_VBUSCT;
			term.printf("VCT %uus\n",ConvTime[idx]);
			setConfig(v);
		} else if (0 == strcmp(args[0],"ict")) {
			if ((m_type != ID_INA226) && (m_type != ID_INA236) && (m_type != ID_INA260))
				return "Operation not supported.";
			int idx = conv_time_index(l);
			if (idx < 0)
				return "Invalid argument #2.";
			if (m_type == ID_INA260) {
				v &= ~MASK_ISHCT;
				v |= idx << SHIFT_ISHCT;
			} else  {
				// INA226, INA236
				v &= ~MASK_VSHCT;
				v |= idx << SHIFT_VSHCT;
			}
			term.printf("ICT %uus\n",ConvTime[idx]);
			setConfig(v);
		} else if (0 == strcmp(args[0],"avg")) {
			// number of conversions to average the result
			// INA226, INA260
			if ((m_type != ID_INA226) && (m_type != ID_INA260))
				return "Operation not supported.";
			if (l <= 0)
				return "Invalid argument #2.";
			return setNumSamples(l);
		} else if (0 == strcmp(args[0],"shunt")) {
			// only for devices with external shunt
			float f = strtof(args[1],&e);
			if ((*e) || (f <= 0))
				return "Invalid argument #2.";
			return setShunt(f);
		} else if (0 == strcmp(args[0],"mode")) {
			// same for all INA2xx
			if (0 == strcmp(args[1],"off")) {
				v &= ~CONF_MODE;
			} else if (0 == strcmp(args[1],"bus")) {
				v &= ~CONF_MODE;
				v |= CONF_MODE_CONT | CONF_MODE_BUS;
			} else if (0 == strcmp(args[1],"bus1")) {
				v &= ~CONF_MODE;
				v |= CONF_MODE_BUS;
			} else if (0 == strcmp(args[1],"current")) {
				v &= ~CONF_MODE;
				v |= CONF_MODE_CONT | CONF_MODE_SHUNT;
			} else if (0 == strcmp(args[1],"current1")) {
				v &= ~CONF_MODE;
				v |= CONF_MODE_SHUNT;
			} else if (0 == strcmp(args[1],"both")) {
				v &= ~CONF_MODE;
				v |= CONF_MODE_CONT | CONF_MODE_BUS | CONF_MODE_SHUNT;
			} else if (0 == strcmp(args[1],"both1")) {
				v &= ~CONF_MODE;
				v |= CONF_MODE_BUS | CONF_MODE_SHUNT;
			} else {
				return "Invalid argument #3.";
			}
			setConfig(v);
		} else if (0 == strcasecmp(args[0],"bul")) {
			if ((l < 0) || (l > UINT16_MAX))
				return "Argument out of range.";
			return setAlert(BIT_BUL,l);
		} else if (0 == strcasecmp(args[0],"bol")) {
			if ((l < 0) || (l > UINT16_MAX))
				return "Argument out of range.";
			return setAlert(BIT_BOL,l);
		} else if (0 == strcasecmp(args[0],"sul")) {
			if ((l < 0) || (l > UINT16_MAX))
				return "Argument out of range.";
			return setAlert(BIT_SUL,l);
		} else if (0 == strcasecmp(args[0],"sol")) {
			//float f = strtof(args[1],&e);
			if ((l < 0) || (l > UINT16_MAX))
				return "Argument out of range.";
			return setAlert(BIT_SOL,l);
		} else if (0 == strcasecmp(args[0],"pol")) {
			if ((l < 0) || (l > UINT16_MAX))
				return "Argument out of range.";
			return setAlert(BIT_POL,l);
		} else if (0 == strcasecmp(args[0],"cnvr")) {
			bool en;
			if (arg_bool(args[1],&en))
				return "Invalid argument #2.";
			return setCNVR(en);
		} else {
			return "Invalid argument #2.";
		}
	} else if (argc == 3) {
		char *e, *e2;
		long l = strtol(args[1],&e,0);
		long l2 = strtol(args[2],&e2,0);
		if (0 == strcmp(args[0],"reg")) {
			// for debugging, read register support
			if ((*e) || (l < 0) || (l > 7))
				return "Argument out of range.";
			if ((*e2) || (l2 < 0) || (l2 > UINT16_MAX))
				return "Argument out of range.";
			writeReg(l,l2);
			return 0;
		} else {
			return "Invalid argument #2.";
		}
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}
#endif


void INA2XX::trigger(void *arg)
{
	INA2XX *dev = (INA2XX *)arg;
	if (dev->m_st == st_off)
		dev->m_st = st_trigger;
}


#endif
