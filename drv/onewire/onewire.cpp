/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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

#ifdef CONFIG_ONEWIRE

#include "log.h"
#include "onewire.h"
#include "owdevice.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <driver/gpio.h>
#include <rom/gpio.h>
#include <xtensa/hal.h>
#include <freertos/task.h>
#include <freertos/portmacro.h>

#ifdef CONFIG_IDF_TARGET_ESP32
#include <rom/crc.h>
#define ENTER_CRITICAL() portDISABLE_INTERRUPTS()
#define EXIT_CRITICAL() portENABLE_INTERRUPTS()
#elif defined CONFIG_IDF_TARGET_ESP8266
#include <esp8266/gpio_struct.h>
#include <esp_crc.h>
#define ENTER_CRITICAL() portENTER_CRITICAL()
#define EXIT_CRITICAL() portEXIT_CRITICAL()
#endif

#include "onewire.h"

using namespace std;

#define OW_MATCH_ROM		0x55
#define OW_SKIP_ROM		0xCC
#define OW_SEARCH_ROM		0xF0
#define OW_READ_ROM		0x33
#define OW_CONDITIONAL_SEARCH	0xEC
#define OW_OVERDRIVE_SKIP_ROM	0x3C
#define OW_OVERDRIVE_MATCH_ROM	0x69
#define OW_SHORT_CIRCUIT	0xFF
#define OW_SEARCH_FIRST		0xFF
#define OW_PRESENCE_ERR		0x01
#define OW_DATA_ERR		0xFE
#define OW_LAST_DEVICE		0x00


static char TAG[] = "owb";


OneWire *OneWire::Instance = 0;


/* 
 * imported from OneWireNg, BSD-2 license
 */
static uint8_t crc8(uint8_t crc, const uint8_t *in, size_t len)
{
	static const uint8_t CRC8_16L[] = {
		0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83,
		0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41
	};
	static const uint8_t CRC8_16H[] = {
		0x00, 0x9d, 0x23, 0xbe, 0x46, 0xdb, 0x65, 0xf8,
		0x8c, 0x11, 0xaf, 0x32, 0xca, 0x57, 0xe9, 0x74
	};
	while (len--) {
		crc ^= *in++;
		crc = CRC8_16L[(crc & 0x0f)] ^ CRC8_16H[(crc >> 4)];
	}
	return crc;
}


OneWire::OneWire(unsigned bus, bool pullup)
: m_bus((gpio_num_t) bus)
{
	assert(Instance == 0);
	Instance = this;
	// idle bus is output, high!
	gpio_pad_select_gpio(m_bus);
	if (esp_err_t e = gpio_set_direction(m_bus,GPIO_MODE_OUTPUT))
		log_warn(TAG,"cannot init to output %d: %s",m_bus,esp_err_to_name(e));
	if (esp_err_t e = gpio_set_level(m_bus,0))
		log_error(TAG,"set lvl2 %s",esp_err_to_name(e));
	if (esp_err_t e = gpio_set_direction(m_bus,GPIO_MODE_INPUT))
		log_warn(TAG,"cannot init to input %d: %s",m_bus,esp_err_to_name(e));
	if (pullup) {
		if (esp_err_t e = gpio_pullup_en(m_bus))
			log_warn(TAG,"cannot enable pullup %d: %s",m_bus,esp_err_to_name(e));
	} else {
		if (esp_err_t e = gpio_pullup_dis(m_bus))
			log_warn(TAG,"cannot disable pullup %d: %s",m_bus,esp_err_to_name(e));
	}
	log_dbug(TAG,"init");
}


int OneWire::addDevice(uint64_t id)
{
	log_dbug(TAG,"add device " IDFMT,IDARG(id));
	uint8_t crc = crc8(0,(uint8_t*)&id,7);
	if (crc != (id >> 56)) {
		log_warn(TAG,"CRC error");
		return 1;
	}
	if (OwDevice::getDevice(id)) {
		log_dbug(TAG,"device " IDFMT " is already registered",IDARG(id));
		return 1;
	}
	log_dbug(TAG,"add device " IDFMT,IDARG(id));
	return OwDevice::create(id);
}


int OneWire::scanBus()
{
	//log_dbug(TAG,"scanBus(%llx)",id);
	vector<uint64_t> collisions;
	uint64_t id = 0;
	do {
		log_dbug(TAG,"searchRom(" IDFMT ",%d)",IDARG(id),collisions.size());
		if (int e = searchRom(id,collisions)) {
			log_error(TAG,"searchRom(" IDFMT ",%d):%d",IDARG(id),collisions.size(),e);
			return 1;	// error occured
		}
		log_dbug(TAG,"searchRom(): " IDFMT,IDARG(id));
		for (auto c : collisions)
			log_dbug(TAG,"collision %08lx%08lx",(uint32_t)(c>>32),(uint32_t)c);
		if (id)
			addDevice(id);
		if (collisions.empty()) {
			id = 0;
		} else {
			id = collisions.back();
			collisions.pop_back();
			vTaskDelay(10);
		}
	} while (id);
	return 0;
}


int OneWire::searchRom(uint64_t &id, vector<uint64_t> &coll)
{
	if (resetBus()) {
		log_error(TAG,"reset failed");
		return OW_PRESENCE_ERR;
	}
	uint64_t x0 = 0, x1 = 0;
	uint8_t c = writeByte(OW_SEARCH_ROM);
	if (OW_SEARCH_ROM != c) {
		log_error(TAG,"search ROM command failed: %02x",c);
		return 1;
	}
	int r = 0;
	ENTER_CRITICAL();
	for (unsigned b = 0; b < 64; ++b) {
		uint8_t t0 = xmitBit(1);
		uint8_t t1 = xmitBit(1);
//		log_dbug(TAG,"t0=%u,t1=%u",t0,t1);
		if (t0 & t1) {
			r = -1;	// data error
			break;
		}
		x0 |= (uint64_t)t0 << b;
		x1 |= (uint64_t)t1 << b;
		if (t0)
			id |= 1LL<<b;
		if ((t1|t0) == 0) {
			// collision
//			log_dbug(TAG,"collission id %x",id|1<<b);
			if ((id >> b) & 1) {	// collission seen before
				xmitBit(1);
			} else {
				coll.push_back(id|1<<b);
				xmitBit(0);
			}
		} else
			xmitBit(t0);
	}
	EXIT_CRITICAL();
	//log_info(TAG,"x0=%016lx, x1=%016lx",x0,x1);
	log_dbug(TAG,"x0=" IDFMT,IDARG(x0));
	log_dbug(TAG,"x1=" IDFMT,IDARG(x1));
	log_dbug(TAG,"id=" IDFMT,IDARG(id));
	return r;
}


int OneWire::readRom()
{
	if (resetBus()) {
		log_error(TAG,"reset failed");
		return OW_PRESENCE_ERR;
	}
	uint8_t r = writeByte(OW_READ_ROM);
	if (OW_READ_ROM != r) {
		log_error(TAG,"search ROM command failed: %02x",r);
		return 1;
	}
	uint8_t id[8];
	for (int i = 0; i < 8; ++i)
		id[i] = readByte();
	uint8_t crc = crc8(0,id,7);
	if (crc == id[7])
		log_info(TAG,"CRC ok");
	else
		log_warn(TAG,"crc mismatch: expected %02x, got %02x",id[7],crc);
	uint64_t id64 = 0;
	for (int i = 0; i < sizeof(id); ++i)
		id64 |= id[i] << (i<<3);
	addDevice(id64);
	log_info(TAG,"read ROM: %02x %02x %02x %02x %02x %02x %02x %02x",id[0],id[1],id[2],id[3],id[4],id[5],id[6],id[7]);
	return 0;
}


int OneWire::resetBus(void)
{
	ENTER_CRITICAL();
	gpio_set_direction(m_bus,GPIO_MODE_OUTPUT);
	gpio_set_level(m_bus,0);
	ets_delay_us(600);
	gpio_set_direction(m_bus,GPIO_MODE_INPUT);
	gpio_pullup_en(m_bus);
	ets_delay_us(60);
	int r = 1;
	for (int i = 0; i < 18; ++i) {
		if (0 == gpio_get_level(m_bus))
			r = 0;
		ets_delay_us(10);
	}
	ets_delay_us(230);
	EXIT_CRITICAL();
	if (r)
		log_error(TAG,"reset: no response");
	return r;
}



unsigned OneWire::xmitBit(uint8_t b)
{
	// TESTED OK
	// idle bus is input pull-up
	unsigned r;
	ets_delay_us(2);
	gpio_set_direction(m_bus,GPIO_MODE_OUTPUT);
	if (b) {
		ets_delay_us(3);
		gpio_set_direction(m_bus,GPIO_MODE_INPUT);
		ets_delay_us(10);
		r = gpio_get_level(m_bus);
		ets_delay_us(65);
	} else {
		ets_delay_us(70);
		gpio_set_direction(m_bus,GPIO_MODE_INPUT);
		ets_delay_us(10);
		r = 0;
	}
	//log_dbug(TAG,"xmit(%d): %d",b,r);
	return r;
}


uint8_t OneWire::writeByte(uint8_t b)
{
	ENTER_CRITICAL();
	uint8_t r = 0;
	for (int i = 0; i < 8; ++i) {
		if (xmitBit(b & (1<<i)))
			r |= (1 << i);
	}
	EXIT_CRITICAL();
	log_dbug(TAG,"writeByte(0x%02x) = 0x%02x",b,r);
	return r;
}


uint8_t OneWire::readByte()
{
	// read by sending 0xff (a dontcare?)
	return writeByte(0xFF);
}


int OneWire::sendCommand(uint64_t id, uint8_t command)
{
	log_dbug(TAG,"sendCommand(" IDFMT ",%02x)",IDARG(id),command);
	resetBus();
	if (id) {
		uint8_t c = writeByte(OW_MATCH_ROM);            // to a single device
		if (c != OW_MATCH_ROM) {
			log_warn(TAG,"match rom command failed");
			return 1;
		}
		for (unsigned i = 0; i < sizeof(id); ++i) {
			writeByte(id&0xff);
			id >>= 8;
		}
	} else {
		writeByte(OW_SKIP_ROM);            // to all devices
	}
	writeByte(command);
	return 0;
}


#endif
