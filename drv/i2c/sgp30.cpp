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
#include "ujson.h"
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


uint8_t crc8_0x31(uint8_t *data, unsigned len, uint8_t crc = 0xff)
{
	static const unsigned char crc8_table[256] = {
		0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97,
		0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
		0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4,
		0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
		0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11,
		0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
		0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52,
		0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
		0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA,
		0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
		0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9,
		0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
		0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C,
		0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
		0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F,
		0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
		0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED,
		0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
		0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE,
		0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
		0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B,
		0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
		0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28,
		0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
		0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0,
		0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
		0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93,
		0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
		0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56,
		0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
		0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15,
		0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
	};
	while (len--)
		crc = crc8_table[crc ^ *data++];
	return crc;
}


static float relHumid2abs(float temp, float humid)
{
	return 10E5 * 18.016/8314.3 * humid/100 * 6.1078 * pow10f((7.5*temp)/(237.3+temp))/(temp + 273.15);
}


static int sgp30_present(uint8_t bus)
{
	uint8_t data[6];
	uint8_t cmd[] = { SGP30_ADDR, 0x36, 0x82 };
	if (i2c_write(bus,cmd,sizeof(cmd),false,true))
		return 1;
	// 0.5ms delay before read required
	vTaskDelay(1/portTICK_PERIOD_MS);
	if (i2c_read(bus,SGP30_ADDR,data,sizeof(data)))
		return 1;
	log_info(TAG,"serial id %02x.%02x.%02x.%02x.%02x.%02x",data[0],data[1],data[2],data[3],data[4],data[5]);
	return 0;
}


void SGP30::attach(JsonObject *r)
{
	log_dbug(TAG,"attach");
	m_root = r;
	m_tvoc = r->add("TVOC",NAN,"ppb");
	m_co2 = r->add("CO2",NAN,"ppm");
}


SGP30 *SGP30::create(uint8_t bus)
{
	uint8_t vercmd[] = { SGP30_ADDR, REG_BASE, REG_GET_VERS };
	if (i2c_write(bus,vercmd,sizeof(vercmd),false,true)) {
		log_warn(TAG,"got serial but reject ver cmd");
//		return 0;
	} else {
		vTaskDelay(2);
		uint8_t version[3];
		if (i2c_read(bus,SGP30_ADDR,version,sizeof(version)))
			return 0;
		uint8_t crc = crc8_0x31(version,2);
		if (crc != version[2]) {
			log_warn(TAG,"version CRC error: calc %02x, got %02x",crc,version[2]);
		}
		log_dbug(TAG,"version %d, type 0x%x",version[1],version[0]>>4);
	}
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
			return 15;
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
	cyclic_add_task("sgp30",SGP30::cyclic,this,220);
	return 0;
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
		if (JsonElement *e = m_root->find("temperature")) {
			if (JsonNumber *n = e->toNumber()) {
				log_dbug(TAG,"found temperature");
				m_temp = n;
			}
		}
		if (JsonElement *e = m_root->find("humidity")) {
			if (JsonNumber *n = e->toNumber()) {
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
	if ((abshumid >= 256) && (abshumid <= 0))
		return;
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
	if (i2c_read(m_bus,SGP30_ADDR,data,sizeof(data))) {
		log_warn(TAG,"read req failed");
		if (m_co2)
			m_co2->set(NAN);
		if (m_tvoc)
			m_tvoc->set(NAN);
		return 1;
	}
//	log_hex(TAG,data,sizeof(data),"airq");
	if (crc8_0x31(data,2) == data[2]) {
		uint16_t co2eq = data[0] << 8 | data[1];
		m_co2->set(co2eq);
		log_dbug(TAG,"co2=%u",co2eq);
	} else {
		m_co2->set(NAN);
		log_warn(TAG,"co2eq CRC error");
	}
	if (crc8_0x31(data+3,2) == data[5]) {
		uint16_t tvoc = data[3] << 8 | data[4];
		if (m_tvoc)
			m_tvoc->set(tvoc);
		log_dbug(TAG,"tvoc=%u",tvoc);
	} else {
		if (m_tvoc)
			m_tvoc->set(NAN);
		log_warn(TAG,"tcov CRC error");
	}
	return 0;
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
	if (sgp30_present(bus))
		return 0;
	return SGP30::create(bus) != 0;
}

#endif
