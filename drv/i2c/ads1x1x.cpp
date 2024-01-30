/*
 *  Copyright (C) 2024, Thomas Maier-Komor
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

#ifdef CONFIG_ADS1X1X

#include "actions.h"
#include "ads1x1x.h"
#include "cyclic.h"
#include "log.h"
#include "terminal.h"
#include "xio.h"

#define ISR_SUPPORT

#define TAG MODULE_ADS1X

#define ADDR_MIN 0x48
#define ADDR_MAX 0x4b

#define REG_CONV 0
#define REG_CONF 1
#define REG_LOTH 2
#define REG_HITH 3

#define BIT_OS		0x8000
#define BIT_MODE	0x0100
#define BIT_COMP	0x0010
#define BIT_POL		0x0008
#define BIT_LAT		0x0004

#define MASK_MUX	0x7000
#define MASK_PGA	0x0e00
#define MASK_DR		0x00e0
#define MASK_QUE	0x0003

#define SHIFT_MUX	12
#define SHIFT_PGA	9
#define SHIFT_DR	5
#define SHIFT_QUE	0

#define GET_MUX(x) ((x & MASK_MUX) >> SHIFT_MUX)
#define GET_PGA(x) ((x & MASK_PGA) >> SHIFT_PGA)
#define GET_DR(x) ((x & MASK_DR) >> SHIFT_DR)
#define GET_QUE(x) ((x & MASK_QUE) >> SHIFT_QUE)

#define CFG_DFLT_HI	0x85
#define CFG_DFLT_LO	0x83
#define CFG_DFLT	0x8583

// Device differences:
// ADS1x13 has no gain, comparator and queue
// ADS1x15 is the only one with MUX

// TODO
// - add gain config support
// - fix SPS on ADS1015


static ADS1x1x *First = 0;
static const uint16_t SPS10[] = { 128, 250, 490, 920, 1600, 2400, 3300, 3300 };
static const uint16_t SPS11[] = { 8, 16, 32, 64, 128, 250, 475, 860 };
static const float Gain[] = { 6.144, 4.096, 2.048, 1.024, 0.512, 0.256, 0.256, 0.256 };
//static uint16_t Delays[] = { 125, 63, 31, 16, 8, 4, 2, 1 };
static uint8_t Delays10[] = { 8, 4, 2, 1, 1, 1, 1, 1 };
static uint8_t Delays11[] = { 20, 20, 10, 8, 8, 4, 2, 1 };
static const char *DevNames[] =
	{ "ads1013"
	, "ads1014"
	, "ads1015"
	, "ads1113"
	, "ads1114"
	, "ads1115"
};


ADS1x1x::ADS1x1x(uint8_t port, uint8_t addr, dev_type_t t)
: I2CDevice(port,addr,DevNames[t])
, m_ad0("ad0",0.0,"","%5.0f")
, m_v0("ad0U",0.0,"mV")
, m_ad1("ad1",0.0,"","%5.0f")
, m_v1("ad1U",0.0,"mV")
, m_ad2("ad2",0.0,"","%5.0f")
, m_v2("ad2U",0.0,"mV")
, m_ad3("ad3",0.0,"","%5.0f")
, m_v3("ad3U",0.0,"mV")
, m_next(First)
, m_cfg(CFG_DFLT)
, m_type(t)
{
	writeConfig();
	if ((ads1013 == t) || (ads1113 == t))
		m_gain = 256;
	First = this;
}


void ADS1x1x::addIntr(uint8_t gpio)
{
#ifdef ISR_SUPPORT
	m_isrev = event_register(m_name,"`isr");
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = xio_cfg_pull_up;
	cfg.cfg_intr = xio_cfg_intr_fall;
	if (0 > xio_config(gpio,cfg)) {
		log_warn(TAG,"gpio %u as interrupt failed",gpio);
	} else if (xio_set_intr(gpio,intrHandler,this)) {
		log_warn(TAG,"add handler for gpio %u interrupt failed",gpio);
	} else {
		Action *a = action_add(concat(m_name,"!read"),readdata,this,0);
		event_callback(m_isrev,a);
		cyclic_rm_task(m_name);
		log_info(TAG,"attached to GPIO%u for interrupts",gpio);
	}
#else
	log_warn(TAG,"use of interrupt not compiled in");
#endif
}


void ADS1x1x::attach(EnvObject *root)
{
	root->add(&m_ad0);
	root->add(&m_v0);
	root->add(&m_ad1);
	root->add(&m_v1);
	root->add(&m_ad2);
	root->add(&m_v2);
	root->add(&m_ad3);
	root->add(&m_v3);
	cyclic_add_task(m_name,ADS1x1x::cyclic,this,0);
	action_add(concat(m_name,"!sample"),sample,(void*)this,"sample data");
	if ((m_type == ads1015) || (m_type == ads1115))
		action_add(concat(m_name,"!set_ch"),set_ch,0,"set channel");
}


ADS1x1x *ADS1x1x::create(uint8_t bus, uint8_t addr, dev_type_t t)
{
	uint8_t cfg[2];
	if (esp_err_t e = i2c_w1rd(bus,addr,1,cfg,sizeof(cfg))) {
		log_warn(TAG,"error reading config: %s",esp_err_to_name(e));
		return 0;
	}
	return new ADS1x1x(bus,addr,t);
}


unsigned ADS1x1x::cyclic(void *arg)
{
	ADS1x1x *drv = (ADS1x1x *) arg;
	if ((drv->m_cfg & BIT_MODE) == 0) {
		int c = readConfig(drv->m_bus,drv->m_addr);
		if (c < 0)
			return 1000;
		uint16_t cfg = c;
		// conitnuous conversion mode
		if (cfg & BIT_OS) {
			// data ready / conversion done
			drv->read();
		}
		return ((drv->m_type <= ads1015) ? Delays10 : Delays11)[(cfg & MASK_DR) >> SHIFT_DR];
	} else {
		if (drv->m_wait) {
			int c = readConfig(drv->m_bus,drv->m_addr);
			if (c < 0)
				return 1000;
			uint16_t cfg = c;
			if (cfg & BIT_OS) {
				// data ready / conversion done
				drv->read();
			}
		}
		if (drv->m_sample && !drv->m_wait) {
			drv->m_cfg |= BIT_OS;
			drv->writeConfig();
			return ((drv->m_type <= ads1015) ? Delays10 : Delays11)[(drv->m_cfg & MASK_DR) >> SHIFT_DR];
		}
	}
	return 10;
}


#ifdef CONFIG_I2C_XCMD
const char *ADS1x1x::exeCmd(Terminal &term, int argc, const char **args)
{
	static const uint16_t ch[] =
		{ (('0' << 8) | '1')
		, (('0' << 8) | '3')
		, (('1' << 8) | '3')
		, (('2' << 8) | '3')
		, (('0' << 8) | 'G')
		, (('1' << 8) | 'G')
		, (('2' << 8) | 'G')
		, (('3' << 8) | 'G')
	};
	if (argc == 0) {
		uint16_t c = ch[GET_MUX(m_cfg)];
		term.printf("mode %s\nsps %d\n"
			, m_cfg & BIT_MODE ? "single" : "continuous"
			, m_type <= ads1015 ? SPS10[GET_DR(m_cfg)] : SPS11[GET_DR(m_cfg)]
		);
		if ((m_type != ads1013) || (m_type != ads1113)) {
			term.printf("gain %g\n"
				, Gain[GET_PGA(m_cfg)]
			);
		}
		if ((m_type == ads1015) || (m_type == ads1115)) {
			term.printf("channel %c,%c\n"
				, (char)(c >> 8), (char) (c & 0xff)
			);
		}
	} else if (argc != 2) {
		return "Invalid number of arguments.";
	} else if (0 == strcmp("-h",args[0])) {
		term.println(
			"mode <single>|<cont>  # sampling mode\n"
			"sps <s>               # set data-rate\n"
			"channel <i>,<r>       # ads1x15 only\n"
			"gain <g>              # not ads1x13\n"
			"low <l>               # not ads1x13\n"
			"high <h>              # not ads1x13\n"
			"itv <i>               # comparator interval (0 or 2..4), not ads1x13\n"
		);
	} else if (0 == strcmp("mode",args[0])) {
		if ((0 == strcmp("single",args[1])) || (0 == strcmp("1",args[1]))) {
			setContinuous(false);
		}  else if ((0 == strcmp("cont",args[1])) || (0 == strcmp("0",args[1]))) {
			setContinuous(true);
		} else {
			return "Invalid argument #2.";
		}
	} else if ((0 == strcmp("ch",args[0])) || (0 == strcmp("channel",args[0]))) {
		if ((m_type != ads1015) && (m_type != ads1115))
			return "Not supported on this device.";
		if (setChannel(args[1]))
			return "Invalid arguemnt #2.";
	} else if ((0 == strcmp("gain",args[0])) || (0 == strcmp("pga",args[0]))) {
		if ((m_type == ads1013) && (m_type == ads1113))
			return "Not supported on this device.";
		char *e;
		float g = strtof(args[1],&e);
		if ((*e) || (setGain(g)))
			return "Invalid arguemnt #2.";
	} else if (0 == strcmp("low",args[0])) {
		if ((m_type == ads1013) && (m_type == ads1113))
			return "Not supported on this device.";
		char *e;
		long v = strtol(args[1],&e,0);
		if ((*e) || (v < 0) || (v > UINT16_MAX) || (setLo(v)))
			return "Invalid arguemnt #2.";
	} else if (0 == strcmp("high",args[0])) {
		if ((m_type == ads1013) && (m_type == ads1113))
			return "Not supported on this device.";
		char *e;
		long v = strtol(args[1],&e,0);
		if ((*e) || (v < 0) || (v > UINT16_MAX) || (setHi(v)))
			return "Invalid arguemnt #2.";
	} else if (0 == strcmp("sps",args[0])) {
		char *e;
		long l = strtol(args[1],&e,0);
		if ((*e != 0) || (setSps(l)))
			return "Invalid arguemnt #2.";
	} else if (0 == strcmp("itv",args[0])) {
		if ((m_type == ads1013) && (m_type == ads1113))
			return "Not supported on this device.";
		char *e;
		long l = strtol(args[1],&e,0);
		if ((*e != 0) || (setInterval(l)))
			return "Invalid arguemnt #2.";
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}
#endif


int ADS1x1x::init()
{
	return 0;
}


void ADS1x1x::intrHandler(void *arg)
{
#ifdef ISR_SUPPORT
	ADS1x1x *dev = (ADS1x1x *) arg;
	event_isr_trigger(dev->m_isrev);
#endif
}


int32_t ADS1x1x::readConfig(uint8_t bus, uint8_t addr)
{
	uint8_t cfg[2];
	if (esp_err_t e = i2c_w1rd(bus,addr,REG_CONF,cfg,sizeof(cfg))) {
		log_dbug(TAG,"error reading config: %s",esp_err_to_name(e));
		return -1;
	}
	return (cfg[0] << 8) | cfg[1];
}


void ADS1x1x::readdata(void *arg)
{
#ifdef ISR_SUPPORT
	ADS1x1x *dev = (ADS1x1x *) arg;
	dev->read();
#endif
}


int ADS1x1x::read()
{
	uint8_t data[2];
	if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_CONV,data,sizeof(data))) {
		log_dbug(TAG,"error reading config: %s",esp_err_to_name(e));
		return -1;
	}
	int16_t d = (int16_t)((data[0] << 8) | data[1]);
	float mv = ((float)d/(float)INT16_MAX) * m_gain;
	switch ((m_cfg & MASK_MUX) >> SHIFT_MUX) {
	case 0:
	case 1:
	case 4:
		m_ad0.set(d);
		m_v0.set(mv);
		break;
	case 2:
	case 5:
		m_ad1.set(d);
		m_v1.set(mv);
		break;
	case 3:
	case 6:
		m_ad2.set(d);
		m_v2.set(mv);
		break;
	case 7:
		m_ad3.set(d);
		m_v3.set(mv);
		break;
	default:
		abort();
	}
	m_wait = false;
	return d;
}


void ADS1x1x::sample(void *arg)
{
	ADS1x1x *dev = (ADS1x1x *) arg;
	if (dev->m_wait) {
		dev->m_sample = true;
	} else {
		dev->m_cfg |= BIT_OS;
		dev->writeConfig();
		dev->m_wait = true;
	}
}


int ADS1x1x::setContinuous(bool cont)
{
	if (cont)
		m_cfg &= ~BIT_MODE;
	else
		m_cfg |= BIT_MODE;
	return writeConfig();
}


void ADS1x1x::set_ch(void *arg)
{
	ADS1x1x *dev = First;
	const char *str = (const char *) arg;
	const char *p = str;
	if (char *c = strchr(str,':')) {
		*c = 0;
		p = c+1;
		while (dev && strcmp(dev->m_name,str))
			dev = dev->m_next;
	}
	if (dev) {
		dev->setChannel(p);
	}
}


int ADS1x1x::setChannel(const char *ch)
{
	int8_t cha = -1, chb = -1;
	if ((ch[0] >= '0') && (ch[0] <= '3'))
		cha = ch[0] - '0';
	if (ch[1] == 0) {
	} else if ((ch[1] == ',') && (ch[3] == 0)) {
		if ((ch[2] >= '0') && (ch[2] <= '3'))
			chb = ch[2] - '0';
		else
			return -1;
	} else {
		return -1;
	}
	return setChannel(cha,chb);
}


int ADS1x1x::setChannel(int8_t a, int8_t b)
{
	uint16_t ch = 0;
	if ((a == 0) && (b == 1)) {
	} else if (b == 3) {
		if ((a < 0) || (a > 2))
			return -1;
		ch = a + 1;
	} else if (b == -1) {
		if ((a < 0) || (a > 3))
			return -1;
		ch = a + 4;
	} else {
		return -1;
	}
	m_cfg &= ~MASK_MUX;
	m_cfg |= ch << SHIFT_MUX;
	return writeConfig();
}


int ADS1x1x::setGain(float g)
{
	uint16_t v;
	if ((g >= 0.2) && (g <= 0.3)) {
		v = 5 << 9;	// 0.256
		m_gain = 256;
	} else if ((g >= 0.4) && (g <= 0.6)) {
		v = 4 << 9;	// 0.512
		m_gain = 512;
	} else if ((g >= 0.9) && (g <= 1.1)) {
		v = 3 << 9;	// 1.024
		m_gain = 1024;
	} else if ((g >= 1.9) && (g <= 2.1)) {
		v = 2 << 9;	// 2.048
		m_gain = 2048;
	} else if ((g >= 4.0) && (g <= 4.2)) {
		v = 1 << 9;	// 4.096
		m_gain = 4096;
	} else if ((g >= 6.0) && (g <= 6.2)) {
		v = 0 << 9;	// 6.144
		m_gain = 6144;
	} else {
		return -1;
	}
	m_cfg &= ~MASK_PGA;
	m_cfg |= v;
	return writeConfig();
}


int ADS1x1x::setLo(int16_t v)
{
	uint8_t data[] = { REG_LOTH, (uint8_t)(v >> 8), (uint8_t)(v & 0xff) };
	if (esp_err_t e = i2c_writen(m_bus,m_addr,data,sizeof(data))) {
		log_warn(TAG,"error writing low threshold: %s",esp_err_to_name(e));
		return -1;
	}
	log_dbug(TAG,"set low threshold to 0x%04x",v);
	return 0;
}


int ADS1x1x::setHi(int16_t v)
{
	uint8_t data[] = { REG_LOTH, (uint8_t)(v >> 8), (uint8_t)(v & 0xff) };
	if (esp_err_t e = i2c_writen(m_bus,m_addr,data,sizeof(data))) {
		log_warn(TAG,"error writing high threshold: %s",esp_err_to_name(e));
		return -1;
	}
	log_dbug(TAG,"set high threshold to 0x%04x",v);
	return 0;
}


int ADS1x1x::setInterval(long itv)
{
	uint16_t r;
	switch (itv) {
	case 0:
		r = 3;
		break;
	case 1:
		r = 0;
		break;
	case 2:
		r = 1;
		break;
	case 4:
		r = 2;
		break;
	default:
		return -1;
	}
	m_cfg &= ~MASK_QUE;
	m_cfg |= r;
	return writeConfig();
}


int ADS1x1x::setSps(long sps)
{
	uint8_t v = 0;
	switch (m_type) {
	case ads1013:
	case ads1014:
	case ads1015:
		while (v < sizeof(SPS10)/sizeof(SPS10[0])) {
			if (sps == SPS10[v]) {
				m_cfg &= ~MASK_DR;
				m_cfg |= v << SHIFT_DR;
				return writeConfig();
			}
			++v;
		}
		break;
	case ads1113:
	case ads1114:
	case ads1115:
		while (v < sizeof(SPS11)/sizeof(SPS11[0])) {
			if (sps == SPS11[v]) {
				m_cfg &= ~MASK_DR;
				m_cfg |= v << SHIFT_DR;
				return writeConfig();
			}
			++v;
		}
		break;
	default:
		abort();
	}
	return -1;
}


int ADS1x1x::writeConfig()
{
	uint8_t data[] = { REG_CONF, (uint8_t)(m_cfg >> 8), (uint8_t)(m_cfg & 0xff) };
	esp_err_t e = i2c_writen(m_bus,m_addr,data,sizeof(data));
	if (e)
		log_warn(TAG,"write Config 0x%04x: %s",m_cfg,esp_err_to_name(e));
	else
		log_dbug(TAG,"write Config 0x%04x",m_cfg);
	m_cfg &= ~BIT_OS;	// start conversion flag must be reset
	return e;
}


#endif // CONFIG_ADS1X1X
