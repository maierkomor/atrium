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

#ifdef CONFIG_BH1750

#include "actions.h"
#include "bh1750.h"
#include "cyclic.h"
#include "env.h"
#include "event.h"
#include "log.h"


#define CMD_POWER_OFF	0x00
#define CMD_POWER_ON	0x01
#define CMD_RESET	0x07	// clear data register, does not work when device is off!
#define CMD_CONT_HRES	0x10	// 120ms, 0.5lx
#define CMD_CONT_HRES2	0x11	// 120ms, 1lx
#define CMD_CONT_LRES	0x13	// 16ms, 4lx
#define CMD_ONCE_HRES	0x20
#define CMD_ONCE_HRES2	0x21
#define CMD_ONCE_LRES	0x23
#define CMD_MTIME_H	0x40
#define CMD_MTIME_L	0x42

#define TAG MODULE_BH1750


BH1750::BH1750(uint8_t b, uint8_t a)
: I2CDevice(b,a,"bh1750")
, m_lux(new EnvNumber("illuminance","lx","%4.0f"))
, m_st(st_idle)
{ }


BH1750 *BH1750::create(uint8_t bus, uint8_t addr)
{
	if ((addr != 0b10111000) && (addr != 0b01000110)) {
		log_warn(TAG,"invalid address 0x%x",addr);
		return 0;
	}
	esp_err_t e;
	log_info(TAG,"looking for BH1750");
#if 0 //def ESP32
	// The timeout retry-logic is for handling an esp32-idf v3.3
	// init-bug, which has a work-around in the atrium i2c init
	// code.
	int count = 0;
	do {
		if (++count > 2)
			return 0;
		e = i2c_write1(bus,addr,CMD_POWER_ON);
		if (e && (e != ESP_ERR_TIMEOUT)) {
			log_dbug(TAG,"%u,0x%x not responding: %s",bus,addr>>1,esp_err_to_name(e));
			return 0;
		}
	} while (e == ESP_ERR_TIMEOUT);
#else
	e = i2c_write1(bus,addr,CMD_POWER_ON);
	if (e) {
		log_dbug(TAG,"%u,0x%x not responding: %s",bus,addr>>1,esp_err_to_name(e));
		return 0;
	}
#endif
	e = i2c_write1(bus,addr,CMD_RESET);
	if (e) {
		log_warn(TAG,"%u,0x%x not responding: %s",bus,addr>>1,esp_err_to_name(e));
		return 0;
	}
	log_info(TAG,"device at %u,0x%x",bus,addr>>1);
	return new BH1750(bus,addr);
}


void BH1750::attach(EnvObject *root)
{
	root->add(m_lux);
	cyclic_add_task(m_name,cyclic,this,0);
	action_add(concat(m_name,"!sample"),sample,(void*)this,"BH1750 sample data (120ms,0.5lx)");
	action_add(concat(m_name,"!qsample"),qsample,(void*)this,"BH1750 quick sample data (16ms,4lx)");
	action_add(concat(m_name,"!off"),qsample,(void*)this,"BH1750 power down");
	action_add(concat(m_name,"!on"),qsample,(void*)this,"BH1750 power up");
}


unsigned BH1750::cyclic(void *arg)
{
	BH1750 *dev = (BH1750 *)arg;
	uint8_t data[2];

	switch (dev->m_st) {
	case st_idle:
		switch (dev->m_rq) {
		case rq_sample:
			if (esp_err_t e = i2c_write1(dev->m_bus,dev->m_addr,CMD_ONCE_HRES)) {
				log_dbug(TAG,"hres: %s",esp_err_to_name(e));
				dev->m_lux->set(NAN);
				dev->m_st = st_err;
			} else {
				log_dbug(TAG,"hres-once ok");
				dev->m_st = st_sampling;
				dev->m_rq = rq_none;
				return 185;
			}
			break;
		case rq_qsample:
			if (esp_err_t e = i2c_write1(dev->m_bus,dev->m_addr,CMD_ONCE_LRES)) {
				log_dbug(TAG,"lres: %s",esp_err_to_name(e));
				dev->m_lux->set(NAN);
				dev->m_st = st_err;
			} else {
				log_dbug(TAG,"lres-once ok");
				dev->m_st = st_sampling;
				return 40;
			}
			break;
		case rq_reset:
			if (esp_err_t e = i2c_write1(dev->m_bus,dev->m_addr,CMD_RESET)) {
				log_dbug(TAG,"reset: %s",esp_err_to_name(e));
				dev->m_st = st_err;
			} else {
				log_dbug(TAG,"reset ok");
				dev->m_st = st_idle;
			}
			dev->m_lux->set(NAN);
			break;
		case rq_off:
			if (esp_err_t e = i2c_write1(dev->m_bus,dev->m_addr,CMD_POWER_OFF)) {
				log_dbug(TAG,"power-off: %s",esp_err_to_name(e));
				dev->m_st = st_err;
			} else {
				log_dbug(TAG,"power-off");
				dev->m_st = st_off;
			}
			dev->m_lux->set(NAN);
			break;
		case rq_on:
		case rq_none:
			break;
		default:
			abort();
		}
		dev->m_rq = rq_none;
		break;
	case st_off:
		if (dev->m_rq == rq_on) {
			if (esp_err_t e = i2c_write1(dev->m_bus,dev->m_addr,CMD_POWER_ON)) {
				log_dbug(TAG,"power-on: %s",esp_err_to_name(e));
				dev->m_st = st_err;
			} else {
				log_dbug(TAG,"power-on");
				dev->m_st = st_idle;
			}
		}
		// discard any other requests
		dev->m_rq = rq_none;
		break;
	case st_sampling:
		if (esp_err_t e = i2c_read(dev->m_bus,dev->m_addr,data,sizeof(data))) {
			log_dbug(TAG,"read data: %s",esp_err_to_name(e));
			dev->m_lux->set(NAN);
			dev->m_st = st_err;
		} else {
			log_dbug(TAG,"data: %02x %02x",data[0],data[1]);
			dev->m_lux->set(((float)(data[0]<<8|data[1]))/1.2);
			dev->m_st = st_idle;
		}
		break;
	case st_err:
		if (dev->m_rq == rq_reset) {
			if (esp_err_t e = i2c_write1(dev->m_bus,dev->m_addr,CMD_RESET))
				log_dbug(TAG,"reset: %s",esp_err_to_name(e));
			else
				dev->m_st = st_idle;
		} else if (dev->m_rq == rq_off) {
			if (esp_err_t e = i2c_write1(dev->m_bus,dev->m_addr,CMD_POWER_OFF)) {
				log_dbug(TAG,"power-off: %s",esp_err_to_name(e));
			} else {
				log_dbug(TAG,"power-off");
				dev->m_st = st_off;
			}
		}
		dev->m_rq = rq_none;
		break;
	default:
		abort();
	}
	return 20;
}


void BH1750::sample(void *arg)
{
	BH1750 *dev = (BH1750 *)arg;
	if (dev->m_rq == rq_none) {
		dev->m_rq = rq_sample;
		log_dbug(TAG,"request sample");
	} else {
		log_dbug(TAG,"request collision");
	}
}


void BH1750::qsample(void *arg)
{
	BH1750 *dev = (BH1750 *)arg;
	if (dev->m_rq == rq_none) {
		dev->m_rq = rq_qsample;
		log_dbug(TAG,"request quick sample");
	} else {
		log_dbug(TAG,"request collision");
	}
}


void BH1750::on(void *arg)
{
	BH1750 *dev = (BH1750 *)arg;
	if (dev->m_rq == rq_none) {
		dev->m_rq = rq_on;
		log_dbug(TAG,"request on");
	} else {
		log_dbug(TAG,"request collision");
	}
}


void BH1750::off(void *arg)
{
	BH1750 *dev = (BH1750 *)arg;
	if (dev->m_rq == rq_none) {
		dev->m_rq = rq_off;
		log_dbug(TAG,"request off");
	} else {
		log_dbug(TAG,"request collision");
	}
}


void BH1750::reset(void *arg)
{
	BH1750 *dev = (BH1750 *)arg;
	if (dev->m_rq == rq_none) {
		dev->m_rq = rq_reset;
		log_dbug(TAG,"request reset");
	} else {
		log_dbug(TAG,"request collision");
	}
}


int bh1750_scan(uint8_t bus)
{
	// auto-scan does not work reliable
	unsigned ret = 0;
	if (BH1750::create(bus,0b01000110))
		++ret;
	if (BH1750::create(bus,0b10111000))
		++ret;
	return ret;
}

#endif
