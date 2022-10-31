/*
 *  Copyright (C) 2021, Thomas Maier-Komor
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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#define SGP30_ADDR	(0x58<<1)

#define REG_BASE	0x20
#define REG_INIT_AIRQ	0x03
#define REG_MEAS_AIRQ	0x08
#define REG_MEAS_TEST	0x32
#define REG_GET_BASEL	0x15
#define REG_SET_BASEL	0x1e
#define REG_GET_VERS	0x2f
#define REG_SET_HUMID	0x61


#define TAG MODULE_SGP30


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
	return 10E5 * 18.016/8314.3 * humid/100 * 6.1078 * pow10f((7.5*temp)/(237.3+temp))/(temp + 273.15);
}


SGP30::SGP30(uint8_t port)
: I2CDevice(port,SGP30_ADDR,drvName())
, m_tvoc("TVOC","ppb","%4.0f")
, m_co2("CO2","ppm")
{
}


SGP30::SGP30(uint8_t port, uint8_t addr, const char *n)
: I2CDevice(port,addr,n)
, m_tvoc("TVOC","ppb","%4.0f")
, m_co2("CO2","ppm")
{
}


void SGP30::attach(EnvObject *r)
{
	log_dbug(TAG,"attach");
	m_root = r;
	r->add(&m_tvoc);
	r->add(&m_co2);
}


SGP30 *SGP30::create(uint8_t bus)
{
	uint8_t cmd[] = { SGP30_ADDR, 0x36, 0x82 };
	if (i2c_write(bus,cmd,sizeof(cmd),false,true))
		return 0;
	// 0.5ms delay before read required
	vTaskDelay(1/portTICK_PERIOD_MS);
	uint8_t data[6];
	if (i2c_read(bus,SGP30_ADDR,data,sizeof(data)))
		return 0;
	log_info(TAG,"serial id %02x.%02x.%02x.%02x.%02x.%02x",data[0],data[1],data[2],data[3],data[4],data[5]);
	uint8_t vercmd[] = { SGP30_ADDR, REG_BASE, REG_GET_VERS };
	if (i2c_write(bus,vercmd,sizeof(vercmd),false,true)) {
		log_warn(TAG,"got serial but reject ver cmd");
		return 0;
	}
	vTaskDelay(2/portTICK_PERIOD_MS);
	uint8_t version[3];
	if (i2c_read(bus,SGP30_ADDR,version,sizeof(version)))
		return 0;
	uint8_t crc = crc8_0x31(version,2);
	if (crc != version[2]) {
		log_warn(TAG,"version CRC error: calc %02x, got %02x",crc,version[2]);
	}
	log_info(TAG,"version %d, type 0x%x",version[1],version[0]>>4);
	SGP30 *d = new SGP30(bus);
	if (d->init()) {
		//delete d;
		return 0;
	}
	return d;
}


const char *SGP30::drvName() const
{
	return "sgp30";
}


unsigned SGP30::cyclic(void *arg)
{
	SGP30 *drv = (SGP30 *)arg;
	switch (drv->m_state) {
	case selftest:
		if (0 == drv->selftest_finish()) {
			drv->m_state = idle;
			return 1000;
		}
		break;
	case measure:
		if (0 == drv->read()) {
			drv->m_state = idle;
			return 975;
		}
		break;
	case idle:
		drv->updateHumidity();
		drv->m_state = update_humid;
		return 10;
	case update_humid:
		if (0 == drv->sample()) {
			drv->m_state = measure;
			return 20;
		}
		break;
	case error:
		if (0 == drv->selftest_start()) {
			drv->m_state = selftest;
			return 220;
		}
		return 10000;
	}
	log_dbug(TAG,"had error");
	drv->m_state = error;
	return 5000;
}


int SGP30::init()
{
	if (0 == selftest_start())
		m_state = selftest;
	else
		m_state = error;
	return cyclic_add_task("sgp30",SGP30::cyclic,this,220);
}


int SGP30::selftest_start()
{
	uint8_t cmd[] = { SGP30_ADDR, REG_BASE, REG_MEAS_TEST };
	if (i2c_write(m_bus,cmd,sizeof(cmd),false,true)) {
		return 1;
	}
	log_dbug(TAG,"selftest started");
	return 0;
}


int SGP30::selftest_finish()
{
	uint8_t selftest[3];
	if (i2c_read(m_bus,SGP30_ADDR,selftest,sizeof(selftest))) {
		log_dbug(TAG,"selftest read failed");
	} else if ((selftest[0] != 0xd4) || (selftest[1] != 0)) {
		log_warn(TAG,"selftest failure %x",(((unsigned)selftest[1])<<8)|((unsigned)selftest[0]));
	} else if (crc8_0x31(selftest,2) != selftest[2])
		log_warn(TAG,"selftest CRC error");
	else
		log_dbug(TAG,"selftest OK");
	if (i2c_write2(m_bus,SGP30_ADDR, REG_BASE, REG_INIT_AIRQ)) {
		log_dbug(TAG,"init req failed");
		return 1;
	}
	log_dbug(TAG,"init device");
	if (m_root) {
		if (EnvElement *e = m_root->find("temperature")) {
			if (EnvNumber *n = e->toNumber()) {
				log_dbug(TAG,"found temperature");
				m_temp = n;
			}
		}
		if (EnvElement *e = m_root->find("humidity")) {
			if (EnvNumber *n = e->toNumber()) {
				log_dbug(TAG,"found humidity");
				m_humid = n;
			}
		}
		if (m_humid == 0)
			log_dbug(TAG,"no humidity sensor");
		if (m_temp == 0)
			log_dbug(TAG,"no temperature sensor");
	}
	return 0;
}


void SGP30::updateHumidity()
{
	if ((m_humid == 0) || (m_temp == 0))
		return;
	double temp = m_temp->get();
	double rhumid = m_humid->get();
	if (isnan(temp) || isnan(rhumid)) {
		log_dbug(TAG,"no humidity info");
		return;
	}
	float abshumid = relHumid2abs(temp,rhumid);
	if ((abshumid >= 256) || (abshumid <= 0)) {
		log_dbug(TAG,"abs humidity out of range %u",(unsigned)abshumid);
		return;
	}
	uint16_t humid = (uint16_t)rintf(abshumid * 256.0);
	if (humid == m_ahumid)
		return;
	m_ahumid = humid;
	uint8_t cmd[] = { SGP30_ADDR, REG_BASE, REG_SET_HUMID, (uint8_t)(humid >> 8), (uint8_t)(humid & 0xff), 0 };
	cmd[sizeof(cmd)-1] = crc8_0x31(cmd+3,2);
	if (i2c_write(m_bus,cmd,sizeof(cmd),true,true))
		log_warn(TAG,"set humidity failed");
	else
		log_dbug(TAG,"humidity %u/256",(unsigned)humid);
}


int SGP30::sample()
{
	if (i2c_write2(m_bus,SGP30_ADDR, REG_BASE, REG_MEAS_AIRQ)) {
		log_dbug(TAG,"sample req failed");
		return 1;
	}
//	log_dbug(TAG,"sample");
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

/*
float relHumid2abs(float temp, float humid, float press)
{
	float I1 = temp;	// degC
	float tK = I1 + 273.15;	// degK
	float tK2 = tK * tK;	// degK^2
	float I2 = humid;	// %
	float H = I2/100;	// relative Humidity
	float I3 = press;	// mBar
	float P = press/1000;	// Bar
	return 0.622 * H * (1.01325 * 10^(5.426651 - 2005.1 / tK + 0.00013869 * (tK2 - 293700) / tK * (10^(0.000000000011965 * (tK2 - 293700) * (tK2 - 293700)) - 1) - 0.0044 * 10^((-0.0057148 * (374.11 - I1)^1.25))) + ((tK / 647.3) - 0.422) * (0.577 - (tK / 647.3)) * EXP(0.000000000011965 * (tK2 - 293700) * (tK2 - 293700)) * 0.00980665) / (P - H * (1.01325 * 10^(5.426651 - 2005.1 / tK + 0.00013869 * (tK2 - 293700) / tK * (10^(0.000000000011965 * (tK2 - 293700) * (tK2 - 293700)) - 1) - 0.0044 * 10^((-0.0057148 * (374.11 - I1)^1.25))) + ((tK / 647.3) - 0.422) * (0.577 - (tK / 647.3)) * EXP(0.000000000011965 * (tK2 - 293700) * (tK2 - 293700)) * 0.00980665)) * P * 100000000 / (tK * 287.1);
}
*/


unsigned sgp30_scan(uint8_t bus)
{
	return SGP30::create(bus) != 0;
}

#endif
