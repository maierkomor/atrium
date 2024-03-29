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

#ifdef CONFIG_SGP30

#include "cyclic.h"
#include "i2cdrv.h"
#include "env.h"
#include "log.h"
#include "sgp30.h"
#include "terminal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#define SGP30_ADDR	(0x58<<1)

#define REG_BASE	0x20
#define REG_INIT_AIRQ	0x03
#define REG_MEAS_AIRQ	0x08
#define REG_MEAS_TEST	0x32
#define REG_MEAS_RAW	0x50
#define REG_GET_BASEL	0x15
#define REG_SET_BASEL	0x1e
#define REG_GET_VERS	0x2f
#define REG_GET_TVOCBL	0xb3
#define REG_SET_TVOCBL	0x77
#define REG_SET_HUMID	0x61


#define TAG MODULE_SGP30


static const char *States[] = {
	"<none>",
	"init",
	"bist",
	"get-serial",
	"get-version",
	"read-bist",
	"read-data",
	"read-serial",
	"read-version",
	"iaq",
	"idle",
	"update",
	"measure",
	"error",
};


static uint8_t crc8_0x31(uint8_t *data, unsigned len, uint8_t crc = 0xff)
{
	do {
		crc ^= *data++;
		for (int i = 0; i < 8; ++i) {
			uint8_t x = crc & 0x80;
			crc <<= 1;
			if (x)
				crc ^= 0x31;
		}
	} while (--len);
	return crc;
}


static inline float relHumid2abs(float temp, float humid)
{
	return ((0.000002*temp*temp*temp*temp)+(0.0002*temp*temp*temp)+(0.0095*temp*temp)+(0.337*temp)+4.9034) * humid / 100;
}


SGP30::SGP30(uint8_t port)
: I2CDevice(port,SGP30_ADDR,drvName())
, m_tvoc("TVOC","ppb","%4.0f")
, m_co2("CO2","ppm")
{
	bzero(m_serial,sizeof(m_serial));
}


SGP30::SGP30(uint8_t port, uint8_t addr, const char *n)
: I2CDevice(port,addr,n)
, m_tvoc("TVOC","ppb","%4.0f")
, m_co2("CO2","ppm")
{
	bzero(m_serial,sizeof(m_serial));
}


void SGP30::attach(EnvObject *r)
{
	log_dbug(TAG,"attach");
	r->add(&m_tvoc);
	r->add(&m_co2);
}


SGP30 *SGP30::create(uint8_t bus)
{
	SGP30 *d = new SGP30(bus);
	d->init();
	return d;
}


const char *SGP30::drvName() const
{
	return "sgp30";
}


unsigned SGP30::cyclic(void *arg)
{
	SGP30 *drv = (SGP30 *)arg;
	return drv->cyclic();
}


unsigned SGP30::cyclic()
{
	esp_err_t e = 0;
	switch (m_state) {
	case st_none:
		return 1000;
	case st_init:
		if ((m_flags & f_ver) == 0)
			m_state = st_getv;
		else if ((m_flags & f_ser) == 0)
			m_state = st_gets;
		else if ((m_flags & f_bist) == 0)
			m_state = st_bist;
		else if ((m_flags & f_iaq) == 0)
			m_state = st_iaq;
		else
			m_state = st_measure;
		return 20;
	case st_bist:
		e = selftest_start();
		if (0 == e) {
			m_state = st_readb;
			return 250;
		}
		break;
	case st_gets:
		e = get_serial();
		if (0 == e) {
			m_state = st_reads;
			return 10;
		}
		break;
	case st_getv:
		e = get_version();
		if (0 == e) {
			m_state = st_readv;
			return 10;
		}
		break;
	case st_readb:
		e = selftest_finish();
		if (0 == e) {
			m_state = st_iaq;
			return 10;
		}
		break;
	case st_readd:
		e = read();
		if (0 == e) {
			m_state = st_update;
			return 975;
		}
		break;
	case st_reads:
		e = read_serial();
		if (0 == e) {
			m_flags |= f_ser;
			m_state = st_init;
			return 10;
		}
		break;
	case st_readv:
		e = read_version();
		if (0 == e) {
			m_flags |= f_ver;
			m_state = st_init;
			return 10;
		}
		break;
	case st_idle:
		return 50;
	case st_update:
		e = updateHumidity();
		if (0 == e) {
			m_state = st_measure;
			return 10;
		}
		break;
	case st_iaq:
		e = init_airq();
		if (0 == e) {
			m_flags |= f_iaq;
			m_state = st_measure;
			return 10;
		}
		break;
	case st_measure:
		e = sample();
		if (0 == e) {
			m_state = st_readd;
			return 20;
		}
		break;
	case st_error:
		m_state = st_init;
		return 3000;
	}
	log_dbug(TAG,"had error");
	m_err = e;
	m_state = st_error;
	m_tvoc.set(NAN);
	m_co2.set(NAN);
	return 5000;
}


#ifdef CONFIG_I2C_XCMD
const char *SGP30::exeCmd(Terminal &term, int argc, const char **args)
{
	if (argc == 0) {
		term.printf("state: %s\n",States[m_state]);
		term.printf("error: %s\n",esp_err_to_name(m_err));
	} else if (0 == strcmp("id",args[0])) {
		uint8_t cmd[] = { SGP30_ADDR, 0x36, 0x82 };	// get serial ID
		if (esp_err_t e = i2c_write(m_bus,cmd,sizeof(cmd),false,true))
			return esp_err_to_name(e);
		// 0.5ms delay before read required
		vTaskDelay(1/portTICK_PERIOD_MS);
		uint8_t data[6];
		if (esp_err_t e = i2c_read(m_bus,SGP30_ADDR,data,sizeof(data)))
			return esp_err_to_name(e);
		term.printf("serial id %02x.%02x.%02x.%02x.%02x.%02x\n",data[0],data[1],data[2],data[3],data[4],data[5]);
	} else if (0 == strcmp("reset",args[0])) {
		m_state = st_init;
	} else if (0 == strcmp("stop",args[0])) {
		m_state = st_idle;
	} else if (0 == strcmp("version",args[0])) {
		term.printf("version %d\n",m_ver & 0xff);
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}
#endif


int SGP30::init()
{
	int r = cyclic_add_task("sgp30",SGP30::cyclic,this,220);
	m_state = st_init;
	return r;
}


int SGP30::init_airq()
{
	log_dbug(TAG,"init airq");
	esp_err_t e = i2c_write2(m_bus,SGP30_ADDR, REG_BASE, REG_INIT_AIRQ);
	if (e) {
		log_dbug(TAG,"init airq failed");
	}
	return e;
}


int SGP30::get_serial()
{
	uint8_t cmd[] = { SGP30_ADDR, 0x36, 0x82 };	// get serial ID
	esp_err_t e = i2c_write(m_bus,cmd,sizeof(cmd),false,true);
	if (e) {
		log_dbug(TAG,"get serial id: %s",esp_err_to_name(e));
	}
	return 0;
}


int SGP30::get_version()
{
	uint8_t vercmd[] = { SGP30_ADDR, REG_BASE, REG_GET_VERS };
	esp_err_t e = i2c_write(m_bus,vercmd,sizeof(vercmd),false,true);
	if (e) {
		log_warn(TAG,"get version: %s",esp_err_to_name(e));
	} else {
		log_dbug(TAG,"get version");
	}
	return e;
}


int SGP30::read_serial()
{
	esp_err_t e = i2c_read(m_bus,SGP30_ADDR,m_serial,sizeof(m_serial));
	if (e) {
		log_dbug(TAG,"read serial id: %s",esp_err_to_name(e));
	} else {
		log_info(TAG,"serial id %02x.%02x.%02x.%02x.%02x.%02x",m_serial[0],m_serial[1],m_serial[2],m_serial[3],m_serial[4],m_serial[5]);
	}
	return e;
}


int SGP30::read_version()
{
	uint8_t version[3];
	esp_err_t e = i2c_read(m_bus,SGP30_ADDR,version,sizeof(version));
	if (e) {
		log_warn(TAG,"read version: %s",esp_err_to_name(e));
	} else {
		uint8_t crc = crc8_0x31(version,2);
		if (crc != version[2]) {
			log_warn(TAG,"version CRC error: calc %02x, got %02x",crc,version[2]);
			e = ESP_ERR_INVALID_CRC;
		} else {
			if (version[1] & 0xd0) {
				log_warn(TAG,"unexpected product id 0x%x",version[1]>>4);
			} else {
				m_ver = (version[1] << 8) | version[0];
				log_info(TAG,"version %d",m_ver);
			}
		}
	}
	return e;
}


int SGP30::selftest_start()
{
	uint8_t cmd[] = { SGP30_ADDR, REG_BASE, REG_MEAS_TEST };
	esp_err_t e = i2c_write(m_bus,cmd,sizeof(cmd),false,true);
	if (e) {
		log_warn(TAG,"selftest failed");
	} else {
		log_dbug(TAG,"selftest started");
	}
	return e;
}


int SGP30::selftest_finish()
{
	uint8_t selftest[3];
	esp_err_t e = i2c_read(m_bus,SGP30_ADDR,selftest,sizeof(selftest));
	if (e) {
		log_warn(TAG,"selftest read failed: %s",esp_err_to_name(e));
	} else {
		log_hex(TAG,selftest,sizeof(selftest),"selftest");
		if ((selftest[0] != 0xd4) || (selftest[1] != 0)) {
			log_warn(TAG,"selftest failure %x",(((unsigned)selftest[1])<<8)|((unsigned)selftest[0]));
			e = ESP_ERR_INVALID_RESPONSE;
		} else if (crc8_0x31(selftest,2) != selftest[2]) {
			log_warn(TAG,"selftest CRC error");
			e = ESP_ERR_INVALID_CRC;
		} else {
			log_dbug(TAG,"selftest OK");
		}
	}
	return e;
}


int SGP30::updateHumidity()
{
	if (m_temp == 0) {
		if (EnvObject *r = m_tvoc.getParent()) {
			if (EnvElement *e = r->find("temperature")) {
				if (EnvNumber *n = e->toNumber()) {
					log_dbug(TAG,"found temperature sensor");
					m_temp = n;
				}
			}
		}
	}
	if (m_humid == 0) {
		if (EnvObject *r = m_tvoc.getParent()) {
			if (EnvElement *e = r->find("humidity")) {
				if (EnvNumber *n = e->toNumber()) {
					log_dbug(TAG,"found humidity sensor");
					m_humid = n;
				}
			}
		}
	}
	float temp = m_temp ? m_temp->get() : NAN;
	float rhumid = m_humid ? m_humid->get() : NAN;
	uint16_t humid;
	if (isnan(temp) || isnan(rhumid)) {
		log_dbug(TAG,"no humidity info");
		humid = 0; // disables humidity compensation
	} else {
		float abshumid = relHumid2abs(temp,rhumid);
		log_dbug(TAG,"temperatue %g, rel humid %g, abs humidity %g",temp,rhumid,abshumid);
		if ((abshumid >= 256) || (abshumid <= 0)) {
			log_dbug(TAG,"abs humidity out of range %u",(unsigned)abshumid);
			humid = 0;
		} else {
			humid = (uint16_t)rintf(abshumid);
		}
	}
	if (humid == m_ahumid)
		return 0;
	m_ahumid = humid;
	uint8_t cmd[] = { SGP30_ADDR, REG_BASE, REG_SET_HUMID, (uint8_t)(humid >> 8), (uint8_t)(humid & 0xff), 0 };
	cmd[sizeof(cmd)-1] = crc8_0x31(cmd+3,2);
	if (esp_err_t e = i2c_write(m_bus,cmd,sizeof(cmd),true,true)) {
		log_warn(TAG,"set humidity failed");
		return e;
	}
	log_dbug(TAG,"humidity %g",(float)humid/256.0);
	return 0;
}


int SGP30::sample()
{
	if (esp_err_t e = i2c_write2(m_bus,SGP30_ADDR, REG_BASE, REG_MEAS_AIRQ)) {
		log_dbug(TAG,"sample req failed");
		return e;
	}
	log_dbug(TAG,"sample");
	return 0;
}


int SGP30::read()
{
	uint8_t data[6];
	double co2 = NAN, tvoc = NAN;
	int r = 0;
	if (i2c_read(m_bus,SGP30_ADDR,data,sizeof(data))) {
		log_warn(TAG,"read req failed");
		r = 1;
	} else {
		//	log_hex(TAG,data,sizeof(data),"airq");
		uint8_t crc = crc8_0x31(data,2);
		if (crc == data[2]) {
			uint16_t co2eq = data[0] << 8 | data[1];
			co2 = co2eq;
			log_dbug(TAG,"co2=%u",co2eq);
		} else {
			log_warn(TAG,"co2eq CRC error: got %x, expected %x",crc,data[2]);
			m_state = st_error;
		}
		uint8_t crc2 = crc8_0x31(data+3,2);
		if (crc2 == data[5]) {
			uint16_t tvocu = data[3] << 8 | data[4];
			tvoc = tvocu;
			log_dbug(TAG,"tvoc=%u",tvocu);
		} else {
			log_warn(TAG,"tcov CRC error: get %x, expected %x",crc2,data[5]);
		}
	}
	m_co2.set(co2);
	m_tvoc.set(tvoc);
	return r;
}


unsigned sgp30_scan(uint8_t bus)
{
	uint8_t vercmd[] = { SGP30_ADDR, REG_BASE, REG_GET_VERS };
	if (0 != i2c_write(bus,vercmd,sizeof(vercmd),false,true))
		return 0;
	vTaskDelay(5);
	uint8_t version[3];
	if (0 != i2c_read(bus,SGP30_ADDR,version,sizeof(version)))
		return 0;
	uint8_t crc = crc8_0x31(version,2);
	if (crc != version[2]) {
		log_dbug(TAG,"version CRC mismatch");
		return 0;
	}
	log_info(TAG,"SGP30 %02x %02x",version[0],version[1]);
	return SGP30::create(bus) != 0;
}

#endif
