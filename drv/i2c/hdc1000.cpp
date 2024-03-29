/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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

#ifdef CONFIG_HDC1000

#include "actions.h"
#include "cyclic.h"
#include "env.h"
#include "event.h"
#include "hdc1000.h"
#include "i2cdrv.h"
#include "log.h"
#include "terminal.h"

#define REG_TEMP	0x00
#define REG_HUMID	0x01
#define REG_CONFIG	0x02
#define REG_SER01	0xfb
#define REG_SER23	0xfc
#define REG_SER45	0xfd

#define BIT_RESET	(1<<15)
#define BIT_HEATER	(1<<13)
#define BIT_MEASBOTH	(1<<12)

#define MASK_TRES	(1<<10)
#define MASK_HRES	(3<<8)
#define SHIFT_HRES	8

#define TAG MODULE_HDC1000


static const char *States[] = {
	"idle","readtemp","readhumid","readboth",
};


HDC1000::HDC1000(uint8_t port, uint8_t addr, const char *name)
: I2CDevice(port,addr,name)
, m_drvname(name)
, m_temp("temperature","\u00b0C")
, m_humid("humidity","%")
{
	uint8_t data[2];
	if (0 == i2c_w1rd(port,addr,REG_CONFIG,data,sizeof(data))) {
		m_cfg = (data[0] << 8) | data[1];
		log_dbug(TAG,"config register 0x%04x",m_cfg);
	}
}


HDC1000 *HDC1000::create(uint8_t bus, uint8_t addr, uint16_t id)
{
	const char *name = 0;
	switch (id) {
	case 0x1000:
		name = "hdc1000";
		break;
	case 0x1050:
		name = "hdc1080";
		break;
	default:
		return 0;
	}
	return new HDC1000(bus,addr,name);
}


void HDC1000::setSingle(bool single)
{
	// independet measure	[12] = 0=single, 1=both
	// 14 bittemperature:	[10] = 0
	// 14bit humidity:	[9,8] = 00
	if (single)
		m_cfg &= ~BIT_MEASBOTH;
	else
		m_cfg |= BIT_MEASBOTH;
	m_cfgsynced = false;
}


void HDC1000::setHeater(bool on)
{
	uint16_t cfg = m_cfg;
	if (on)
		cfg |= BIT_HEATER;
	else
		cfg &= ~BIT_HEATER;
	m_cfgsynced = (cfg != m_cfg);
	m_cfg = cfg;
}


unsigned HDC1000::cyclic()
{
	switch (m_state) {
	case st_idle:
		if (!m_cfgsynced) {
//			uint16_t cfg = (unsigned) m_tres | (unsigned) m_hres;
			uint8_t data[] = { m_addr, REG_CONFIG, (uint8_t)(m_cfg >> 8), (uint8_t)(m_cfg & 0xff) };
			if (esp_err_t e = i2c_write(m_bus,data,sizeof(data),1,1)) {
				log_warn(TAG,"config sync failed: %s",esp_err_to_name(e));
				break;
			}
			m_cfgsynced = true;
		}
		if (m_sample == sm_both) {
			log_dbug(TAG,"sample both");
			if (i2c_write1(m_bus,m_addr,REG_TEMP))
				break;
			m_sample = sm_none;
			m_state = st_readboth;
			return 15;
		}
		if (m_sample & sm_temp) {
			log_dbug(TAG,"sample temp");
			if (i2c_write1(m_bus,m_addr,REG_TEMP))
				break;
			m_state = st_readtemp;
			m_sample = (sample_t)(m_sample ^ sm_temp);
			return 8;
		}
		if (m_sample & sm_humid) {
			log_dbug(TAG,"sample humid");
			if (i2c_write1(m_bus, m_addr, REG_HUMID))
				break;
			m_state = st_readhumid;
			m_sample = (sample_t)(m_sample ^ sm_humid);
			return 8;
		}
		return 20;
	case st_readhumid:
		{
			log_dbug(TAG,"read humid");
			uint8_t data[2];
			if (i2c_read(m_bus,m_addr,data,sizeof(data)))
				break;
			setHumid(data);
			m_state = st_idle;
			if (m_sample != sm_none)
				return 5;
			return 50;
		}
	case st_readtemp:
		{
			log_dbug(TAG,"read temp");
			uint8_t data[2];
			if (i2c_read(m_bus,m_addr,data,sizeof(data)))
				break;
			setTemp(data);
			m_state = st_idle;
			if (m_sample != sm_none)
				return 5;
			return 50;
		}
	case st_readboth:
		{
			log_dbug(TAG,"read both");
			uint8_t data[4];
			if (i2c_read(m_bus,m_addr,data,sizeof(data)))
				break;
			setTemp(data);
			setHumid(data+2);
			m_state = st_idle;
			return 50;
		}
	default:
		abort();
	}
	log_dbug(TAG,"error");
	m_state = state_t::st_idle;
	m_temp.set(NAN);
	m_humid.set(NAN);
	return 1000;
}


#ifdef CONFIG_I2C_XCMD
const char *HDC1000::exeCmd(struct Terminal &term, int argc, const char **args)
{
	if (argc == 0) {
		static const uint8_t hres_vals[] = { 14, 11, 8 };
		unsigned tres = m_cfg & MASK_TRES ? 11 : 14;
		unsigned hres = hres_vals[(m_cfg & MASK_HRES) >> SHIFT_HRES];
		const char *heat = (m_cfg & BIT_HEATER) ? "on" : "off";
		const char *mode = (m_cfg & BIT_MEASBOTH) ? "both" : "single";
		term.printf(
			"state: %s\n"
			"temperature: %ubits\n"
			"humidity   : %ubits\n"
			"heater     : %s\n"
			"mode       : %s\n"
			, States[m_state]
			, tres
			, hres
			, heat
			, mode
		);
		return 0;
	}
	if (0 == strcmp(args[0],"-h")) {
		term.println(
			"valid commands:\n"
			"serial    : prints serial id\n"
			"hres [res]: set humid resolution to 8,11,14 bits\n"
			"tres [res]: set humid resolution to 11,14 bits\n"
			"reset     : perform device reset\n"
			"seq       : perform measurements sequential\n"
			"both      : perform measurements together\n"
			);
	} else if (0 == strcmp(args[0],"reset")) {
	} else if (0 == strcmp(args[0],"seq")) {
		setSingle(true);
	} else if (0 == strcmp(args[0],"both")) {
		setSingle(false);
	} else if (0 == strcmp(args[0],"hres")) {
		if (argc == 1)
			return "Missing argument.";
		char *e;
		long l = strtol(args[1],&e,0);
		if (*e)
			return "Invalid argument #2.";
		if (l == 8) {
			m_cfg &= ~MASK_HRES;
			m_cfg |= hres_8b;
			m_cfgsynced = false;
		} else if (l == 11) {
			m_cfg &= ~MASK_HRES;
			m_cfg |= hres_11b;
			m_cfgsynced = false;
		} else if (l == 14) {
			m_cfg &= ~MASK_HRES;
			m_cfg |= hres_14b;
			m_cfgsynced = false;
		} else {
			return "Invalid argument #2.";
		}
	} else if (0 == strcmp(args[0],"tres")) {
		if (argc == 1)
			return "Missing argument.";
		char *e;
		long l = strtol(args[1],&e,0);
		if (*e)
			return "Invalid argument #2.";
		if (l == 11) {
			m_cfg &= ~MASK_TRES;
			m_cfg |= tres_11b;
			m_cfgsynced = false;
		} else if (l == 14) {
			m_cfg &= ~MASK_TRES;
			m_cfg |= tres_14b;
			m_cfgsynced = false;
		} else {
			return "Invalid argument #2.";
		}
	} else if (0 == strcmp(args[0],"heater")) {
		if (0 == strcmp(args[1],"on"))
			setHeater(true);
		else if (0 == strcmp(args[1],"1"))
			setHeater(true);
		else if (0 == strcmp(args[1],"off"))
			setHeater(false);
		else if (0 == strcmp(args[1],"0"))
			setHeater(false);
		else
			return "Invalid argument #2.";
	} else if (0 == strcmp(args[0],"serial")) {
		uint8_t serial[6];
		if (i2c_w1rd(m_bus,m_addr,REG_SER01,serial+0,2))
			return "I2C error.";
		if (i2c_w1rd(m_bus,m_addr,REG_SER23,serial+2,2))
			return "I2C error.";
		if (i2c_w1rd(m_bus,m_addr,REG_SER45,serial+4,2))
			return "I2C error.";
		uint32_t s0 = serial[0] << 24 | serial[1]<<16 | serial[2]<<8 | serial[3];
		uint16_t s1 = serial[4]<<8 | serial[5];
		term.printf("serial id %08x%04x",s0,(unsigned)s1);
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}
#endif


void HDC1000::setHumid(uint8_t data[])
{
	uint16_t humid_raw = (data[0] << 8) | data[1];
	float h = (float)humid_raw / (1<<16) * 165 - 40;
	log_dbug(TAG,"humid %G",h);
	m_humid.set(h);
}


void HDC1000::setTemp(uint8_t data[])
{
	uint16_t temp_raw = (data[0] << 8) | data[1];
	float t = (float)temp_raw / (1<<16) * 165 - 40;
	log_dbug(TAG,"temp %G",t);
	m_temp.set(t);
}


static unsigned hdc1000_cyclic(void *arg)
{
	HDC1000 *drv = (HDC1000 *) arg;
	return drv->cyclic();
}


void HDC1000::trigger(void *arg)
{
	HDC1000 *drv = (HDC1000 *) arg;
	drv->m_sample = sm_both;
	if ((drv->m_cfg & BIT_MEASBOTH) == 0) {
		drv->m_cfg |= BIT_MEASBOTH;
		drv->m_cfgsynced = false;
	}
}


void HDC1000::trigger_temp(void *arg)
{
	HDC1000 *drv = (HDC1000 *) arg;
	drv->m_sample = (sample_t)(drv->m_sample | sm_temp);
	if (drv->m_cfg & BIT_MEASBOTH) {
		drv->m_cfg &= ~BIT_MEASBOTH;
		drv->m_cfgsynced = false;
	}
}


void HDC1000::trigger_humid(void *arg)
{
	HDC1000 *drv = (HDC1000 *) arg;
	drv->m_sample = (sample_t)(drv->m_sample | sm_humid);
	if (drv->m_cfg & BIT_MEASBOTH) {
		drv->m_cfg &= ~BIT_MEASBOTH;
		drv->m_cfgsynced = false;
	}
}


void HDC1000::attach(EnvObject *root)
{
	root->add(&m_temp);
	root->add(&m_humid);
	cyclic_add_task(m_name,hdc1000_cyclic,this,0);
	action_add(concat(m_name,"!sample"),HDC1000::trigger,(void*)this,"HDC1000 sample data");
	action_add(concat(m_name,"!temp"),HDC1000::trigger_temp,(void*)this,"HDC1000 sample temperature");
	action_add(concat(m_name,"!humid"),HDC1000::trigger_humid,(void*)this,"HDC1000 sample humidity");
}


#endif
