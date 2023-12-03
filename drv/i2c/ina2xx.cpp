/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#define TAG MODULE_INA219

#include "actions.h"
#include "cyclic.h"
#include "ina2xx.h"
#include "nvm.h"
#include "log.h"
#include "terminal.h"

#include <esp_err.h>


#define INA_REG_CONF	0x00
#define INA_REG_SHUNT	0x01
#define INA_REG_BUS	0x02
#define INA_REG_POW	0x03
#define INA_REG_AMP	0x04
#define INA_REG_CALIB	0x05
#define INA_REG_MASK	0x06	// ina226 only
#define INA_REG_ALERT	0x07	// ina226 only
#define INA_REG_MANID	0xfe	// ina226 only
#define INA_REG_DIEID	0xff	// ina226 only

#define INA219_ADDR_MIN_ID	0x80
#define INA219_ADDR_MAX_ID	0x8f

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


static uint8_t ConvTimes[] = {2,2,3,5,9,18,35,70};


INA219::INA219(uint8_t bus, uint8_t addr)
: I2CDevice(bus,addr,"ina219")
, m_volt("bus","V","%4.1f")
, m_amp("current","A","%4.4f")
, m_shunt("shunt","mV","%4.3f")
, m_power("power","W","%4.1f")
, m_conf(INA_CONF_RESET_VALUE)
, m_st(st_cont)
, m_delay(2)
{
	log_info(TAG,"ina219 at %d/0x%x",bus,addr);
}


void INA219::attach(class EnvObject *root)
{
	root->add(&m_amp);
	root->add(&m_volt);
	root->add(&m_shunt);
	root->add(&m_power);
	cyclic_add_task(m_name,cyclic,this,0);
	action_add(concat(m_name,"!sample"),trigger,(void*)this,"sample data");
}


INA219 *INA219::create(uint8_t bus, uint8_t addr)
{
	addr <<= 1;
	log_info(TAG,"checking for INA219 at %d/0x%x",bus,addr);
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
	uint16_t rv = (v[0] << 8) | v[1];
	log_dbug(TAG,"config = 0x%x",rv);
	if (rv != INA_CONF_RESET_VALUE) {
		uint8_t reset[] = {addr,INA_REG_CONF,0x80,0x00};
		if (i2c_write(bus,reset,sizeof(reset),1,1))
			return 0;
		if (i2c_w1rd(bus,addr,INA_REG_CONF,v,sizeof(v)))
			return 0;
		rv = (v[0] << 8) | v[1];
		if (rv != INA_CONF_RESET_VALUE)
			log_warn(TAG,"unexpected config %04x",rv);
	}
	char nvsn[32]; 
	sprintf(nvsn,"ina219@%u,%x.cfg",bus,addr);
	uint16_t cfgv = nvm_read_u16(nvsn,0x399f);
	uint8_t cfg[] = {addr,INA_REG_CONF,(uint8_t)(cfgv>>8),(uint8_t)cfgv};
	if (esp_err_t e = i2c_write(bus,cfg,sizeof(cfg),1,1))
		log_warn(TAG,"config failed: %s",esp_err_to_name(e));
	sprintf(nvsn,"ina219@%u,%x.cal",bus,addr);
	uint16_t cal = nvm_read_u16(nvsn,0x5000);
	uint8_t data[] = { addr, INA_REG_CALIB, (uint8_t)(cal >> 8), (uint8_t)(cal >> 0) };
	if (esp_err_t e = i2c_write(bus,data,sizeof(data),1,1))
		log_warn(TAG,"calibration failed: %s",esp_err_to_name(e));
	return new INA219(bus,addr);
}


unsigned INA219::cyclic(void *arg)
{
	INA219 *dev = (INA219 *)arg;
	uint8_t data[2];
	uint16_t bus = 0;
	int16_t shunt = 0, amp = 0;
	bool has_amp = false;
	switch (dev->m_st) {
	case st_read:
		dev->m_st = st_off;
		dev->m_conf = (dev->m_conf & ~CONF_MODE);
		/* FALLTHRU */
	case st_cont:
		if (0 == i2c_w1rd(dev->m_bus,dev->m_addr,INA_REG_SHUNT,data,sizeof(data))) {
			shunt = (int16_t) ((data[0] << 8) | data[1]);
			dev->m_shunt.set(((float)shunt)*1E-2);
		} else {
			dev->m_shunt.set(NAN);
		}
		if (0 == i2c_w1rd(dev->m_bus,dev->m_addr,INA_REG_AMP,data,sizeof(data))) {
			amp = (int16_t) ((data[0] << 8) | data[1]);
			dev->m_amp.set((float)amp/100);
			has_amp = true;
		} else {
			dev->m_amp.set(NAN);
		}
		if (0 == i2c_w1rd(dev->m_bus,dev->m_addr,INA_REG_BUS,data,sizeof(data))) {
			bus = (uint16_t) ((data[0] << 8) | data[1]);
			bus >>= 3;
			dev->m_volt.set((float)bus*0.004);
			if (has_amp)
				dev->m_power.set(dev->m_volt.get()*dev->m_amp.get());
			else
				dev->m_power.set(NAN);
		} else {
			dev->m_volt.set(NAN);
			dev->m_power.set(NAN);
		}
		log_dbug(TAG,"bus %f, shunt %d, amp %d",(float)bus*0.004,shunt,amp);
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
	return dev->m_delay;
}


void INA219::updateDelay()
{
	if ((m_conf & CONF_MODE_CONT) == 0) {
		m_delay = 10;
	} else {
		uint8_t adc = m_badc < m_sadc ? m_sadc : m_badc;
		if (adc & 0x8)
			m_delay = ConvTimes[adc&7];
		else
			m_delay = 2;
	}
	log_dbug(TAG,"delay %u",m_delay);
}


static int sample_cfg(long l)
{
	switch (l) {
	case 9:
	case 10:
	case 11:
	case 12:
		return (l-9);
	case 2:
		return 9;
	case 4:
		return 10;
	case 8:
		return 11;
	case 16:
		return 12;
	case 32:
		return 13;
	case 64:
		return 14;
	case 128:
		return 15;
	default:
		return -1;
	}
}


#ifdef CONFIG_I2C_XCMD
const char *INA219::exeCmd(Terminal &term, int argc, const char **args)
{
	if ((argc == 0) || (0 == strcmp(args[0],"-h"))) {
		term.println(
			"brng <v>: set bus voltage range (valid values: 16, 32)\n"
			"pg <r>  : set shunt range (valid values: 40, 80, 160, 320)\n"
			"badc <r>: set bus-ADC (9..12bits or 2,4,8,16,32,64,128 samples)\n"
			"sadc <r>: set shunt-ADC (9..12bits or 2,4,8,16,32,64,128 samples)\n"
			"cal <c> : set calibration value (0..65535)\n"
			"mode <m>: set mode (off, bus, shunt, both, bus1, shunt1, both1)\n"
			);
		return 0;
	}
	static const uint8_t AdcMap[] = {1,2,4,8,16,32,64,128};
	uint8_t data[2];
	uint16_t v;
	if (esp_err_t e = i2c_w1rd(m_bus,m_addr,INA_REG_CONF,data,sizeof(data))) {
		term.printf("com error: %s\n",esp_err_to_name(e));
		return "";
	}
	v = (data[0] << 8) | data[1];
	m_conf = v;
	if (argc == 1) {
		if (0 == strcmp(args[0],"cal")) {
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,INA_REG_CALIB,(uint8_t*)&v,sizeof(v))) {
				term.printf("com error: %s\n",esp_err_to_name(e));
				return "";
			}
			term.printf("calib %d\n",(uint16_t)(v>>8)|(uint16_t)(v<<8));
		} else if (0 == strcmp(args[0],"conf")) {
			term.printf("conf 0x%x\n",v);
		} else if (0 == strcmp(args[0],"brng")) {
			term.printf("brng %d\n",v & CONF_BRNG ? 32 : 16);
		} else if (0 == strcmp(args[0],"pg")) {
			term.printf("gain /%d\n", 1 << ((v & CONF_PG) >> CONF_BIT_PG));
		} else if (0 == strcmp(args[0],"badc")) {
			uint8_t adc = (v & CONF_BADC) >> CONF_BIT_BADC;
			if (adc & 0x8)
				term.printf("badc %d samples\n", AdcMap[adc&0x7]);
			else
				term.printf("badc %d bit\n", (adc&0x3)+9);
		} else if (0 == strcmp(args[0],"sadc")) {
			uint8_t adc = (v & CONF_SADC) >> CONF_BIT_SADC;
			if (adc & 0x8)
				term.printf("sadc %d samples\n", AdcMap[adc&0x7]);
			else
				term.printf("sadc %d bit\n", (adc&0x3)+9);
		} else if (0 == strcmp(args[0],"mode")) {
			if (v & CONF_MODE)
				term.printf("mode%s%s%s\n",v & CONF_MODE_CONT ? " continuous" : "", CONF_MODE_BUS ? " bus" : "", CONF_MODE_SHUNT ? " shunt" : "");
			else
				term.printf("mode off\n");
		} else if (0 == strcmp(args[0],"reset")) {
			uint8_t data[] = { m_addr, INA_REG_CONF, 0x80, 0x00 };
			if (esp_err_t e = i2c_write(m_bus,data,sizeof(data),1,1))
				return esp_err_to_name(e);
			m_conf = INA_CONF_RESET_VALUE;
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 2) {
		char *e;
		long l = strtol(args[1],&e,0);
		if (0 == strcmp(args[0],"brng")) {
			if (l == 16) {
				v &= ~CONF_BRNG;
			} else if (l == 32) {
				v |= CONF_BRNG;
			} else {
				return "Invalid argument #2.";
			}
		} else if (0 == strcmp(args[0],"pg")) {
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
		} else if (0 == strcmp(args[0],"badc")) {
			v &= ~CONF_BADC;
			int x = sample_cfg(l);
			if (x < 0)
				return "Invalid argument #2.";
			v |= x << CONF_BIT_BADC;
			m_badc = x;
			updateDelay();
		} else if (0 == strcmp(args[0],"sadc")) {
			v &= ~CONF_SADC;
			int x = sample_cfg(l);
			if (x < 0)
				return "Invalid argument #2.";
			v |= x << CONF_BIT_SADC;
			m_sadc = x;
			updateDelay();
		} else if (0 == strcmp(args[0],"cal")) {
			if ((l < 0) || (l > UINT16_MAX)) 
				return "Invalid argument #2.";
			char nvsn[32]; 
			sprintf(nvsn,"ina219@%u,%x.cal",m_bus,m_addr);
			nvm_store_u16(nvsn,l);
			uint8_t data[] = {m_addr,INA_REG_CALIB,(uint8_t)(l>>8),(uint8_t)(l)};
			if (esp_err_t e = i2c_write(m_bus,data,sizeof(data),1,1))
				return esp_err_to_name(e);
			return 0;
		} else if (0 == strcmp(args[0],"mode")) {
			if (0 == strcmp(args[1],"off")) {
				m_conf &= ~CONF_MODE;
			} else if (0 == strcmp(args[1],"bus")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_CONT | CONF_MODE_BUS;
			} else if (0 == strcmp(args[1],"bus1")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_BUS;
			} else if (0 == strcmp(args[1],"shunt")) {
				m_conf &= ~CONF_MODE;
				m_conf |= CONF_MODE_CONT | CONF_MODE_SHUNT;
			} else if (0 == strcmp(args[1],"shunt1")) {
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
			updateDelay();
			v = m_conf;
		} else {
			return "Invalid argument #2.";
		}
		char nvsn[32]; 
		sprintf(nvsn,"ina219@%u,%x.cfg",m_bus,m_addr);
		nvm_store_u16(nvsn,v);
		uint8_t data[] = {m_addr,INA_REG_CONF,(uint8_t)(v>>8),(uint8_t)v};
		if (esp_err_t e = i2c_write(m_bus,data,sizeof(data),1,1)) {
			term.printf("com error: %s\n",esp_err_to_name(e));
			return "";
		}
		m_conf = v;
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}
#endif


void INA219::trigger(void *arg)
{
	INA219 *dev = (INA219 *)arg;
	if (dev->m_st == st_off)
		dev->m_st = st_trigger;
}


#endif
