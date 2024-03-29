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

#ifdef CONFIG_AHTXX

#include "actions.h"
#include "ahtxx.h"
#include "cyclic.h"
#include "log.h"
#include "terminal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


#define AHTXX_TRIGGER	{ 0xac, 0x33, 0x00 }
#define AHTXX_ADDR	0x70	/* 0x38 << 1 */
#define AHTXX_RST  	0xba

#define BIT_BUSY	0x80
#define BIT_CRC		0x10
#define BIT_CAL		0x08
#define BIT_CMP		0x04
#define MASK_MODE	0x60

#define TAG MODULE_AHT

// documented in AhT10 manual, returns error for whatever reason...
static const uint8_t AHT10_INIT[] = { 0xe1, 0xac };	
// documented in AHT20 manual
static const uint8_t AHT20_INIT[] = { 0xbe, 0x08, 0x00 };

static const char *Modes[] = {
	"NOR","CYC","CMD","CMD"
};

static const char *DevNames[] = {
	"aht10", "aht20", "aht21", "aht30"
};



AHTXX::AHTXX(uint8_t bus, uint8_t addr, ahtdev_t d)
: I2CDevice(bus,addr,DevNames[d])
, m_temp("temperature","\u00b0C","%4.1f")
, m_humid("humidity","%","%4.1f")
, m_dev(d)
{

}


void AHTXX::attach(EnvObject *root)
{
	root->add(&m_temp);
	root->add(&m_humid);
	cyclic_add_task(m_name,cyclic,this,0);
	action_add(concat(m_name,"!sample"),sample,(void*)this,"AHTxx sample data");
}


unsigned AHTXX::cyclic(void *arg)
{
	AHTXX *drv = (AHTXX *) arg;
	return drv->cyclic();
}


unsigned AHTXX::cyclic()
{
	switch (m_state) {
	case st_idle:
		break;
	case st_trigger:
		trigger();
		m_state = st_read;
		return 75;
	case st_read:
		if (read())
			return 5;
		m_state = st_idle;
		break;
	default:
		abort();
	}
	return 50;
}


const char *AHTXX::drvName() const
{
	return DevNames[m_dev];
}


#ifdef CONFIG_I2C_XCMD
const char *AHTXX::exeCmd(Terminal &term, int argc, const char **args)
{
	int st = getStatus();
	if (st < 0)
		return "Failed.";
	term.printf("status: %s %s CRC %s %scalibrated%s\n"
		, st & BIT_BUSY ? "busy" : "free"
		, Modes[(st&MASK_MODE)>>5]
		, st & BIT_CRC ? "ok" : "error"
		, st & BIT_CAL ? "" : "un"
		, st & BIT_CMP ? " CMP" : ""
		);
	return 0;
}
#endif


int AHTXX::init(uint8_t bus, ahtdev_t d)
{
	log_dbug(TAG,"init");
	uint8_t addr = AHTXX_ADDR;
	size_t csize = 0;
	const uint8_t *cmd = 0;
	switch (d) {
	case aht10:
		cmd = AHT10_INIT;
		csize = sizeof(AHT10_INIT);
		break;
	case aht20:
		cmd = AHT20_INIT;
		csize = sizeof(AHT20_INIT);
		break;
	case aht30:
		break;
	default:
		abort();
	}
	if (csize) {
		if (esp_err_t e = i2c_writen(bus,addr,cmd,csize)) {
			log_warn(TAG,"init failed: %s",esp_err_to_name(e));
			return e;
		}
	}
	return 0;
}


int AHTXX::getStatus()
{
	uint8_t st;
	if (esp_err_t e = i2c_read(m_bus,m_addr,&st,sizeof(st))) {
		log_warn(TAG,"getStatus: %s",esp_err_to_name(e));
		return -1;
	}
	log_info(TAG,"status: %s %s %scalibrated%s"
		, st & BIT_BUSY ? "busy" : "free"
		, Modes[(st&MASK_MODE)>>5]
		, st & BIT_CAL ? "" : "un"
		, st & BIT_CMP ? " CMP" : ""
		);
	return st;
}


static uint8_t calc_crc8(const uint8_t *data, size_t n)
{
	uint8_t crc = 0xff;
	for (unsigned b = 0; b < n; ++b) {
		crc &= data[b];
		for (unsigned i = 8; i > 0; --i) {
			if (crc & 0x80)
				crc = (crc<<1)^31;
			else
				crc <<= 1;
		}
	}
	return crc;
}


int AHTXX::read()
{
	uint8_t data[6+(aht30 == m_dev)];
	if (esp_err_t e = i2c_read(m_bus,m_addr,data,sizeof(data))) {
		log_warn(TAG,"read: %s",esp_err_to_name(e));
		return -1;
	}
	if (data[0] & BIT_BUSY) {
		log_dbug(TAG,"read: busy");
		return 1;
	}
	uint32_t rh = (data[1] << 12) | (data[2] << 4) | (data[3] >> 4);
	m_humid.set((float)rh/((float)(1<<20))*100);
	uint32_t t = ((data[3] & 0xf) << 16) | (data[4] << 8) | data[5];
	m_temp.set((float)t/((float)(1<<20))*200.0-50.0);
	log_hex(TAG,data,sizeof(data),"rh=%u, t=%u",rh,t);
	if (aht30 == m_dev) {
		uint8_t crc = calc_crc8(data,6);
		if (crc != data[6])
			log_warn(TAG,"crc 0x%02x, calculated 0x%02x",data[6],crc);
	}
	return 0;
}


int AHTXX::reset(uint8_t bus, ahtdev_t d)
{
	uint8_t addr = AHTXX_ADDR, rst = AHTXX_RST;
	switch (d) {
	case aht10:
	case aht20:
		break;
	case aht30:
		return 0;
	default:
		abort();
	}
	return i2c_write1(bus,addr,rst);
}


void AHTXX::sample(void *arg)
{
	AHTXX *drv = (AHTXX *) arg;
	if (st_idle == drv->m_state)
		drv->m_state = st_trigger;
}


void AHTXX::trigger()
{
	uint8_t trig[] = AHTXX_TRIGGER;
	if (esp_err_t e = i2c_writen(m_bus,m_addr,trig,sizeof(trig))) {
		log_warn(TAG,"trigger: %s",esp_err_to_name(e));
	} else {
		log_dbug(TAG,"triggered");
	}
}


int aht_scan(uint8_t bus, AHTXX::ahtdev_t type)
{
	int n = 0;
	log_info(TAG,"scan");
	if (AHTXX::aht30 == type) {
		uint8_t data[7];
		if (0 == i2c_read(bus,AHTXX_ADDR,data,sizeof(data))) {
			new AHTXX(bus,AHTXX_ADDR,type);
			++n;
		}
	} else if (0 == i2c_write1(bus,AHTXX_ADDR,AHTXX_RST)) {
		log_dbug(TAG,"reset ok");
		vTaskDelay(30);
		// init sequence of AHT10 fails
		// AHT10 responds ok to AHT20 init sequence - why?
		// seems also to work without init sequence
		// therefore, failures are ignored
		AHTXX::init(bus,type);
		new AHTXX(bus,AHTXX_ADDR,type);
		++n;
	} else {
		log_warn(TAG,"reset failed");
	}
	return n;
}

#endif // CONFIG_AHT
