/*
 *  Copyright (C) 2018-2024, Thomas Maier-Komor
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
#include "ina2xx.h"
#include "nvm.h"
#include "log.h"
#include "terminal.h"
#include "xio.h"

#include <esp_err.h>


#define INA_REG_CONF	0x00
#define INA_REG_BUS	0x02
#define INA_REG_POW	0x03
#define INA_REG_AMP	0x04
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

/* Reg Map	INA219	INA220	INA226	INA260
 * 00h		cfg	cfg	cfg	cfg
 * 01h		shunt	shunt	shunt	cur
 * 02h		bus	bus	bus	bus
 * 03h		pwr	pwr	pwr	pwr
 * 04h		cur	cur	cur	---
 * 05h		cal	cal	cal	---
 * 06h		---	---	mask	mask
 * 07h		---	---	alert	alert
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

#define ENABLE_LEN	(1<<0)	// alert latch enable
#define ENABLE_APOL	(1<<1)	// alert polarity 0=active-low
#define ENABLE_OVF	(1<<2)	// math overflow
#define ENABLE_CVRF	(1<<3)	// conversion ready flag
#define ENABLE_AFF	(1<<4)	// alert function flag
#define ENABLE_CVR	(1<<10)	// conversion ready
#define ENABLE_POL	(1<<11)	// power over limit
#define ENABLE_BUL	(1<<12)	// bus under-voltage
#define ENABLE_BOL	(1<<13)	// bus over-voltage
#define ENABLE_SUL	(1<<14)	// shunt under-voltage
#define ENABLE_SOL	(1<<15)	// shunt over-voltage

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
		vshct:4,	// shunt conversion time: 0.14ms..8.244ms
		vbusct:4,	// bus conversion time: 0.14ms..8.244ms
		avg:3,		// averaging count 
		reserved:3,
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
	{ }

	const char *drvName() const override
	{ return m_is220?"ina220":"ina219"; }

	void attach(class EnvObject *) override;

	protected:
	void init();
	float getShunt() const override;
	void read() override;
	const char *setNumSamples(unsigned n) override;
	const char *setShunt(float r) override;

	EnvNumber m_shunt;
	float m_res = 0;
	bool m_is220;
};


struct INA226 : public INA2XX
{
	INA226(uint8_t bus, uint8_t addr)
	: INA2XX(bus,addr,ID_INA226,"ina226")
	, m_shunt("shunt","mV","%4.3f")
	{ }

	const char *drvName() const override
	{ return "ina226"; }

	void addIntr(uint8_t gpio) override;
	void attach(class EnvObject *) override;

	protected:
	static void read(void *);

	void init();
	float getShunt() const override;
	void read() override;
	const char *setNumSamples(unsigned n) override;
	const char *setShunt(float r) override;

	EnvNumber m_shunt;
	float m_res = 0;
	uint16_t m_mask = 0;
	event_t m_isrev = 0;
};


struct INA260 : public INA2XX
{
	INA260(uint8_t bus, uint8_t addr)
	: INA2XX(bus,addr,ID_INA260,"ina260")
	{ }

	const char *drvName() const override
	{ return "ina260"; }

	void addIntr(uint8_t gpio) override;
	void attach(class EnvObject *) override;

	protected:
	static void read(void *);

	void init();
	float getShunt() const override;
	void read() override;
	const char *setNumSamples(unsigned n) override;

	uint16_t m_mask = 0;
	event_t m_isrev = 0;
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
	event_t ev = event_register(name,"`isr");
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = xio_cfg_pull_up;
	cfg.cfg_intr = xio_cfg_intr_fall;
	if (0 > xio_config(gpio,cfg)) {
		log_warn(TAG,"gpio %u config failed",gpio);
	} else if (xio_set_intr(gpio,event_isr_handler,(void*)(unsigned)ev)) {
		log_warn(TAG,"gpio%u interrupt failed",gpio);
	} else {
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


INA2XX::INA2XX(uint8_t bus, uint8_t addr, uint8_t type, const char *name)
: I2CDevice(bus,addr,name)
, m_volt("bus","V","%4.1f")
, m_amp("current","A","%4.4f")
, m_power("power","W","%4.1f")
, m_conf(INA_CONF_RESET_VALUE)
, m_st(st_cont)
, m_type(type)
{
}


void INA226::addIntr(uint8_t gpio)
{
	m_isrev = init_alert(m_name,gpio);
	if (m_isrev) {
		if (Action *a = action_add(concat(m_name,"!read"),read,this,0)) {
			event_callback(m_isrev,a);
			uint8_t mask[2];
			i2c_w1rd(m_bus,m_addr,INA_REG_MASK,mask,sizeof(mask));
			m_mask = (mask[1]<<8)|mask[0]|ENABLE_CVR;
			uint8_t data[] = {INA_REG_MASK,(uint8_t)(m_mask>>8),(uint8_t)(m_mask&0xff)};
			if (esp_err_t e = i2c_writen(m_bus,m_addr,data,sizeof(data))) {
				log_warn(TAG,"config alert: %s",esp_err_to_name(e));
			} else {
				log_info(TAG,"alert on GPIO%u",gpio);
			}
		}
	}
}


void INA260::addIntr(uint8_t gpio)
{
	m_isrev = init_alert(m_name,gpio);
	if (m_isrev) {
		if (Action *a = action_add(concat(m_name,"!read"),read,this,0)) {
			event_callback(m_isrev,a);
			m_mask |= ENABLE_CVR;
			uint8_t data[] = {INA_REG_MASK,(uint8_t)(m_mask>>8),(uint8_t)(m_mask&0xff)};
			if (esp_err_t e = i2c_writen(m_bus,m_addr,data,sizeof(data))) {
				log_warn(TAG,"config alert: %s",esp_err_to_name(e));
			} else {
				log_info(TAG,"alert on GPIO%u",gpio);
			}
		}
	}
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
	cyclic_add_task(m_name,cyclic,this,0);
}


void INA226::attach(class EnvObject *root)
{
	init();
	root->add(&m_shunt);
	INA2XX::attach(root);
	if (0 == m_isrev)
		cyclic_add_task(m_name,cyclic,this,0);
}


void INA260::attach(class EnvObject *root)
{
	INA2XX::init();
	INA2XX::attach(root);
	if (0 == m_isrev)
		cyclic_add_task(m_name,cyclic,this,0);
}


INA2XX *INA2XX::create(uint8_t bus, uint8_t addr, uint8_t type)
{
	if ((type == ID_INA219)||(type == ID_INA220)) {
		log_info(TAG,"checking for INA%u at %d/0x%x",type,bus,addr);
		uint8_t v[2];
		if (addr) {
			if (i2c_w1rd(bus,addr,INA_REG_CONF,v,sizeof(v)))
				return 0;
		} else {
			esp_err_t err;
			for (addr = 0x40; addr < 0x50; ++addr) {
				err = i2c_w1rd(bus,addr<<1,INA_REG_CONF,v,sizeof(v));
				if (err == 0)
					break;
			}
			if (err)
				return 0;
			addr <<= 1;
		}
	}
	switch (type) {
	case ID_INA219:
		return new INA219(bus,addr,false);
	case ID_INA220:
		return new INA219(bus,addr,true);
	case ID_INA226:
		return new INA226(bus,addr);
	case ID_INA260:
		return new INA260(bus,addr);
	default:
		;
	}
	return 0;
}


void INA2XX::init()
{
	reset();
	log_info(TAG,"init");
	char nvsn[32];
	sprintf(nvsn,"ina2%02u@%u,%x.cfg",m_type,m_bus,m_addr);
	uint16_t cfgv = nvm_read_u16(nvsn,m_conf);	// use reset value as default
	if (cfgv != m_conf)
		setConfig(cfgv);
}


void INA219::init()
{
	INA2XX::init();
	m_res = nvm_get_res(m_type,m_bus,m_addr);
}


void INA226::init()
{
	INA2XX::init();
	m_res = nvm_get_res(m_type,m_bus,m_addr);
}


void INA2XX::setConfig(uint16_t cfgv)
{
	if (cfgv != m_conf) {
		uint8_t cfg[] = {m_addr,INA_REG_CONF,(uint8_t)(cfgv>>8),(uint8_t)cfgv};
		if (esp_err_t e = i2c_write(m_bus,cfg,sizeof(cfg),1,1)) {
			log_warn(TAG,"config failed: %s",esp_err_to_name(e));
		} else {
			m_conf = cfgv;
			char nvsn[32];
			sprintf(nvsn,"ina2%02u@%u,%x.cfg",m_type,m_bus,m_addr);
			nvm_store_u16(nvsn,m_conf);	// use reset value as default
		}
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


const char *INA2XX::setShunt(float r)
{
	return "Operation not supported.";
}


static void store_shunt(uint8_t type, uint8_t bus, uint8_t addr, float r)
{
	char nvsn[32]; 
	sprintf(nvsn,"ina2%02u@%u,%x.r",type,bus,addr);
	nvm_store_float(nvsn,r);
}


const char *INA219::setShunt(float r)
{
	store_shunt(m_type,m_bus,m_addr,r);
	m_res = r;
	return 0;
}


const char *INA226::setShunt(float r)
{
	store_shunt(m_type,m_bus,m_addr,r);
	m_res = r;
	return 0;
}


unsigned INA2XX::cyclic(void *arg)
{
	INA2XX *dev = (INA2XX *)arg;
	switch (dev->m_st) {
	case st_read:
		dev->m_st = st_off;
		dev->m_conf &= ~CONF_MODE;
		/* FALLTHRU */
	case st_cont:
		dev->read();
		break;
	case st_trigger:
		{
			uint16_t v = (dev->m_conf & ~CONF_MODE) | CONF_MODE_BUS | CONF_MODE_SHUNT;
			if (0 == i2c_write2(dev->m_bus,INA_REG_CONF,(uint8_t)(v>>8),(uint8_t)v)) {
				dev->m_st = st_read;
				dev->m_conf = v;
			}
		}
		break;
	case st_off:
		break;
	default:
		abort();
	}
	return 10;
}


void INA226::read(void *arg)
{
	INA226 *dev = (INA226 *) arg;
	dev->read();
}


void INA260::read(void *arg)
{
	INA260 *dev = (INA260 *) arg;
	dev->read();
}


void INA2XX::read()
{
}


void INA219::read()
{
	float fshnt = NAN, fvlt = NAN;
	int16_t shnt = 0;
	uint16_t vlt = 0;
	uint8_t data[2];
	if (0 == i2c_w1rd(m_bus,m_addr,INA_REG01,data,sizeof(data))) {
		shnt = (int16_t) ((data[0] << 8) | data[1]);
		fshnt = (float)shnt * 0.01;
	}
	if (0 == i2c_w1rd(m_bus,m_addr,INA_REG02,data,sizeof(data))) {
		vlt = (uint16_t) ((data[0] << 8) | data[1]);
		vlt >>= 3;
		fvlt = (float)vlt * 0.004;
	}
	m_shunt.set(fshnt);
	m_volt.set(fvlt);
	float fcur = fshnt / m_res * 1E-3;
	m_amp.set(fcur);
	float fpwr = fcur*fvlt;
	if (fpwr < 0)
		fpwr *= -1;
	m_power.set(fpwr);
	log_dbug(TAG,"shnt %4.3fmV, cur %4.3fA, vlt %4.3fV, pwr %4.3fW, res %4.3f",fshnt,fcur,fvlt,fpwr,m_res);
}


void INA226::read()
{
	uint8_t data[2];
	float fshnt = NAN, fvlt = NAN;
	if (0 == i2c_w1rd(m_bus,m_addr,INA_REG01,data,sizeof(data))) {
		log_dbug(TAG,"reg1 %02x %02x",data[0],data[1]);
		int16_t shnt = (int16_t) ((data[0] << 8) | data[1]);
		fshnt = (float)shnt * 2.5E-6;
	}
	if (0 == i2c_w1rd(m_bus,m_addr,INA_REG02,data,sizeof(data))) {
		log_dbug(TAG,"reg2 %02x %02x",data[0],data[1]);
		uint16_t vlt = (int16_t) ((data[0] << 8) | data[1]);
		fvlt = (float)vlt * 1.25E-3;
	}
	if (m_isrev) {
		if (0 == i2c_w1rd(m_bus,m_addr,INA_REG06,data,sizeof(data)))
			log_dbug(TAG,"reg6 %02x %02x",data[0],data[1]);
		uint8_t data[] = {INA_REG_MASK,(uint8_t)(m_mask>>8),(uint8_t)(m_mask&0xff)};
		if (esp_err_t e = i2c_writen(m_bus,m_addr,data,sizeof(data)))
			log_warn(TAG,"reset mask: %s",esp_err_to_name(e));
	}
	m_shunt.set(fshnt);
	m_volt.set(fvlt);
	float fcur = fshnt / m_res;
	m_amp.set(fcur);
	float fpwr = fcur * fvlt;
	m_power.set(fpwr);
	log_dbug(TAG,"shnt %4.3fV, cur %4.3fA, vlt %4.3fV, pwr %4.3fW",fshnt,fcur,fvlt,fpwr);
}


void INA260::read()
{
	uint8_t data[2];
	float fcur = NAN, fvlt = NAN;
	if (0 == i2c_w1rd(m_bus,m_addr,INA_REG01,data,sizeof(data))) {
		int16_t crr = (int16_t) ((data[0] << 8) | data[1]);
		fcur = (float)crr * 1.25E-3;
		m_amp.set(fcur);
	}
	if (0 == i2c_w1rd(m_bus,m_addr,INA_REG02,data,sizeof(data))) {
		uint16_t vlt = (int16_t) ((data[0] << 8) | data[1]);
		fvlt = (float)vlt * 1.25E-3;
		m_volt.set(fvlt);
	}
	float fpwr = fcur * fvlt;
	m_power.set(fpwr);
	log_dbug(TAG,"cur %4.3fA, vlt %4.3fV, pwr %4.3fW",fcur,fvlt,fpwr);
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


float INA219::getShunt() const
{
	return m_res;
}


float INA226::getShunt() const
{
	return m_res;
}


float INA260::getShunt() const
{
	return 0.002;
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
	uint8_t data[] = { INA_REG_CONF, 0x80, 0x00 };
	esp_err_t e = i2c_writen(m_bus,m_addr,data,sizeof(data));
	if (0 == e) {
		uint8_t cfg[2];
		e = i2c_w1rd(m_bus, m_addr, INA_REG_CONF, cfg, sizeof(cfg));
		if (0 == e) {
			m_conf = (cfg[1]<<8) | cfg[0];
			m_conf &= 0x7fff;	// clear reset bit
			log_info(TAG,"reset: config %04x",m_conf);
		}
	}
	return e;
}


#ifdef CONFIG_I2C_XCMD
const char *INA2XX::exeCmd(Terminal &term, int argc, const char **args)
{
	if ((argc == 0) || (0 == strcmp(args[0],"-h"))) {
		term.println(
			"brng <v> : set bus voltage range (valid values: 16, 32)\n"
			"pg <r>   : set shunt range (valid values: 40, 80, 160, 320)\n"
			"avg <c>  : set number of samples for averaging\n"
			"mode <m> : set mode (off, bus, shunt, both, bus1, shunt1, both1)\n"
			"reset    : reset to power-on defaults\n"
			"shunt <r>: set shunt resistor to <r> Ohm\n"
			"vct <t>  : set bus voltage conversion time\n"
			"ict <t>  : set current conversion time\n"
			"           <t> in {140,204,332,588,1100,2116,4156,8244}\n"
			);
		return 0;
	}
	uint8_t data[2];
	uint16_t v;
	if (esp_err_t e = i2c_w1rd(m_bus,m_addr,INA_REG_CONF,data,sizeof(data))) {
		term.printf("com error: %s\n",esp_err_to_name(e));
		return "";
	}
	v = (data[0] << 8) | data[1];
	if (argc == 1) {
		if (v != m_conf) {
			log_warn(TAG,"hidden conf update: %x => %x",m_conf,v);
			m_conf = v;
		}
		if (0 == strcmp(args[0],"conf")) {
			printMode(term,v);
			if ((ID_INA260 == m_type) || (ID_INA226 == m_type))
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
			term.printf("shunt %4.3f Ohm\n",getShunt());
		} else if (0 == strcmp(args[0],"reset")) {
			reset();
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
		} else if (0 == strcmp(args[0],"conf")) {
			if ((*e) || (l < 0) || (l > UINT16_MAX))
				return "Invalid argument #2.";
			v = l;
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
		} else if (0 == strcmp(args[0],"vct")) {
			if ((m_type != ID_INA226) && (m_type != ID_INA260))
				return "Operation not supported.";
			int idx = conv_time_index(l);
			if (idx < 0)
				return "Invalid argument #2.";
			v &= ~MASK_VBUSCT;
			v |= idx << SHIFT_VBUSCT;
			term.printf("VCT %uus\n",ConvTime[idx]);
		} else if (0 == strcmp(args[0],"ict")) {
			int idx = conv_time_index(l);
			if (idx < 0)
				return "Invalid argument #2.";
			if (m_type == ID_INA226) {
				v &= ~MASK_VSHCT;
				v |= idx << SHIFT_VSHCT;
			} else if (m_type == ID_INA260) {
				v &= ~MASK_ISHCT;
				v |= idx << SHIFT_ISHCT;
			} else {
				return "Operation not supported.";
			}
			term.printf("ICT %uus\n",ConvTime[idx]);
		} else if (0 == strcmp(args[0],"avg")) {
			// number of conversions to average the result
			// INA226, INA260
			if (l <= 0)
				return "Invalid argument #2.";
			return setNumSamples(l);
		} else if (0 == strcmp(args[0],"shunt")) {
			if ((m_type != ID_INA219) && (m_type != ID_INA220) && (m_type != ID_INA226))
				return "Operation not supported.";
			// only for devices with external shunt
			float f = strtof(args[1],&e);
			if ((*e) || (f <= 0))
				return "Invalid argument #2.";
			return setShunt(f);
		} else if (0 == strcmp(args[0],"mode")) {
			// same for all INA2xx
			if (0 == strcmp(args[1],"off")) {
				m_conf &= ~CONF_MODE;
			} else if (0 == strcmp(args[1],"bus")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_CONT | CONF_MODE_BUS;
			} else if (0 == strcmp(args[1],"bus1")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_BUS;
			} else if (0 == strcmp(args[1],"current")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_CONT | CONF_MODE_SHUNT;
			} else if (0 == strcmp(args[1],"current1")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_SHUNT;
			} else if (0 == strcmp(args[1],"both")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_CONT | CONF_MODE_BUS | CONF_MODE_SHUNT;
			} else if (0 == strcmp(args[1],"both1")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_BUS | CONF_MODE_SHUNT;
			} else {
				return "Invalid argument #3.";
			}
			v = m_conf;
		} else if (0 == strcmp(args[0],"reg")) {
			// for debugging, read register support
			char *e;
			long l = strtol(args[1],&e,0);
			if ((*e) || (l < 0) || (l > 6))
				return "Argument out of range.";
			uint8_t data[2];
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,(uint8_t)l,data,sizeof(data)))
				return esp_err_to_name(e);
			int dec = (int)(int16_t)((data[0]<<8)|data[1]);
			term.printf("reg %ld: %02x %02x (dec = %d)\n",l,data[0],data[1],dec);
		} else {
			return "Invalid argument #2.";
		}
		setConfig(v);
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
