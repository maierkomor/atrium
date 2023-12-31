/*
 *  Copyright (C) 2020-2023, Thomas Maier-Komor
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
#include <freertos/task.h>
#include <freertos/portmacro.h>

#if IDF_VERSION >= 50
#define ets_delay_us esp_rom_delay_us
#endif

#ifdef ESP32
#include <rom/crc.h>
#define ENTER_CRITICAL() portDISABLE_INTERRUPTS()
#define EXIT_CRITICAL() portENABLE_INTERRUPTS()
#elif defined CONFIG_IDF_TARGET_ESP8266
#include <esp8266/gpio_struct.h>
#include <esp_crc.h>
#define ENTER_CRITICAL() portENTER_CRITICAL()
#define EXIT_CRITICAL() portEXIT_CRITICAL()
#endif

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


#define TAG MODULE_OWB


OneWire *OneWire::Instance = 0;


/* 
 * imported from OneWireNg, BSD-2 license
 */
uint8_t OneWire::crc8(const uint8_t *in, size_t len)
{
	static const uint8_t CRC8_16L[] = {
		0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83,
		0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41
	};
	static const uint8_t CRC8_16H[] = {
		0x00, 0x9d, 0x23, 0xbe, 0x46, 0xdb, 0x65, 0xf8,
		0x8c, 0x11, 0xaf, 0x32, 0xca, 0x57, 0xe9, 0x74
	};
	uint8_t crc = 0;
	while (len--) {
		crc ^= *in++;
		crc = CRC8_16L[(crc & 0x0f)] ^ CRC8_16H[(crc >> 4)];
	}
	return crc;
}


OneWire::OneWire(xio_t bus, xio_t pwr)
: m_bus(bus)
, m_pwr(pwr)
{
	Instance = this;
	log_info(TAG,"bus at %u",bus);
	if (pwr != XIO_INVALID) {
		log_info(TAG,"power at %u",pwr);
		setPower(true);
	}
}


OneWire *OneWire::create(unsigned bus, bool pullup, int8_t pwr)
{
	assert(Instance == 0);
	// idle bus is output, high!
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_od;
	cfg.cfg_pull = pullup ? xio_cfg_pull_up : xio_cfg_pull_none;
	if (0 > xio_config((xio_t)bus,cfg)) {
		log_warn(TAG,"failed to config xio%u",bus);
		return 0;
	}
	if (xio_set_lvl((xio_t)bus,xio_lvl_hiz)) {
		log_warn(TAG,"cannot set hi %u",bus);
		return 0;
	}
	xio_t p = XIO_INVALID;
	if (pwr != -1) {
		cfg.cfg_io = xio_cfg_io_od;
		cfg.cfg_pull = xio_cfg_pull_none;
		if (0 > xio_config((xio_t)pwr,cfg)) {
			log_warn(TAG,"failed to config power xio%u",bus);
		} else {
			p = (xio_t) pwr;
		}
	}
	return new OneWire((xio_t)bus,p);
}


int OneWire::addDevice(uint64_t id)
{
	uint8_t crc = crc8((uint8_t*)&id,7);
	if (crc != (id >> 56)) {
		log_warn(TAG,"CRC error: calculated 0x%02x, expected 0x%02x",(unsigned)crc,(unsigned)(id>>56));
		return 1;
	}
	if (OwDevice::getDevice(id)) {
		log_dbug(TAG,"device " IDFMT " is already registered",IDARG(id));
		return 1;
	}
	log_dbug(TAG,"add device " IDFMT,IDARG(id));
	return OwDevice::create(id);
}


void OneWire::setPower(bool on)
{
	if (m_pwr != XIO_INVALID) {
		xio_lvl_t l = (on ? xio_lvl_0 : xio_lvl_hiz);
		xio_set_lvl(m_pwr,l);
		m_pwron = on;
	}
}


int OneWire::scanBus()
{
	//log_dbug(TAG,"scanBus(%llx)",id);
	vector<uint64_t> collisions;
	uint64_t id = 0;
	do {
		log_dbug(TAG,"searchRom(" IDFMT ",%d)",IDARG(id),collisions.size());
		int e = searchRom(id,collisions);
		if (e > 0) {
			log_warn(TAG,"searchRom(" IDFMT ",%d):%d",IDARG(id),collisions.size(),e);
			return 1;	// error occured
		}
		if (e < 0)
			log_dbug(TAG,"no response");
		log_dbug(TAG,"searchRom(): " IDFMT,IDARG(id));
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


static IRAM_ATTR unsigned xmitBit(uint8_t bus, uint8_t b)
{
	// TESTED OK
	// idle bus is input pull-up
	unsigned r;
	ets_delay_us(2);
	xio_set_lvl(bus,xio_lvl_0);
	if (b) {
		ets_delay_us(3);
		xio_set_lvl(bus,xio_lvl_hiz);
		ets_delay_us(10);
		r = xio_get_lvl(bus);
		ets_delay_us(65);
	} else {
		ets_delay_us(70);
		xio_set_lvl(bus,xio_lvl_hiz);
		ets_delay_us(10);
		r = 0;
	}
//	log_dbug(TAG,"xmit(%d): %d",b,r);
	return r;
}


static IRAM_ATTR uint8_t writeBits(uint8_t bus, uint8_t byte)
{
//	log_dbug(TAG,"writeBits 0x%02x",byte);
	ENTER_CRITICAL();
	uint8_t r = 0;
	for (uint8_t b = 1; b; b<<=1) {
		if (xmitBit(bus, byte & b))
			r |= b;
	}
	EXIT_CRITICAL();
//	no debug here, as writeBits is used in timinig critical sections!
	return r;
}


static IRAM_ATTR uint64_t searchId(uint8_t bus, uint64_t &xid, vector<uint64_t> &coll)
{
	uint64_t x0 = 0, x1 = 0, id = xid;
	int r = 0;
	ENTER_CRITICAL();
	for (unsigned b = 0; b < 64; ++b) {
		uint8_t t0 = xmitBit(bus, 1);
		uint8_t t1 = xmitBit(bus, 1);
//		log_dbug(TAG,"t0=%u,t1=%u",t0,t1);
		if (t0 & t1) {
			r = -1;	// no response
			break;
		}
		x0 |= (uint64_t)t0 << b;
		x1 |= (uint64_t)t1 << b;
		if (t0)
			id |= 1LL<<b;
		if ((t1|t0) == 0) {
			// collision
//			log_dbug(TAG,"collision id %x",id|1<<b);
			if ((id >> b) & 1) {	// collision seen before
				xmitBit(bus, 1);
			} else {
				coll.push_back(id|1<<b);
				xmitBit(bus, 0);
			}
		} else
			xmitBit(bus, t0);
	}
	EXIT_CRITICAL();
	xid = id;
//	log_dbug(TAG,"x0=%016lx, x1=%016lx",x0,x1);
	return r;
}


int OneWire::searchRom(uint64_t &id, vector<uint64_t> &coll)
{
	if (resetBus()) {
		log_warn(TAG,"reset failed");
		return OW_PRESENCE_ERR;
	}
	setPower(false);
	uint8_t c = writeBits(m_bus,OW_SEARCH_ROM);
	if (OW_SEARCH_ROM != c) {
		log_warn(TAG,"search ROM command failed: %02x",c);
		return 1;
	}
	size_t nc = coll.size();
	log_dbug(TAG,"search id %lx",id);
	int r = searchId(m_bus,id,coll);
	setPower(true);
	if (Modules[TAG]) {
		while (nc < coll.size()) {
			uint64_t c = coll[nc++];
			log_dbug(TAG,"collision %08lx%08lx",(uint32_t)(c>>32),(uint32_t)c);
		}
	}
//	log_dbug(TAG,"x0=%016lx, x1=%016lx",x0,x1);
	return r;
}


int OneWire::readRom()
{
	if (resetBus()) {
		log_error(TAG,"reset failed");
		return OW_PRESENCE_ERR;
	}
	uint8_t r = writeBits(m_bus,OW_READ_ROM);
	if (OW_READ_ROM != r) {
		log_error(TAG,"search ROM command failed: %02x",r);
		return 1;
	}
	uint8_t id[8];
	for (int i = 0; i < 8; ++i)
		id[i] = readByte();
	uint8_t crc = crc8(id,7);
	if (crc == id[7])
		log_dbug(TAG,"CRC ok");
	else
		log_warn(TAG,"crc mismatch: expected %02x, got %02x",id[7],crc);
	uint64_t id64 = 0;
	for (int i = 0; i < sizeof(id); ++i)
		id64 |= id[i] << (i<<3);
	addDevice(id64);
	log_hex(TAG,id,sizeof(id),"read ROM");
	return 0;
}


int OneWire::resetBus(void)
{
	log_dbug(TAG,"reset");
	assert((m_pwr == XIO_INVALID) || (m_pwron == true));
	ENTER_CRITICAL();
	xio_set_lvl(m_bus,xio_lvl_0);
	ets_delay_us(500);
	xio_set_lvl(m_bus,xio_lvl_hiz);
	ets_delay_us(20);
	int r = 1;
	for (int i = 0; i < 26; ++i) {
		if (0 == xio_get_lvl(m_bus)) {
			r = 0;
		}
		ets_delay_us(10);
	}
	ets_delay_us(220);
	EXIT_CRITICAL();
	if (r)
		log_warn(TAG,"reset: no response");
	return r;
}



uint8_t OneWire::writeByte(uint8_t byte)
{
	log_dbug(TAG,"writeByte 0x%02x",byte);
	return writeBits(m_bus,byte);
}


uint8_t OneWire::readByte()
{
	// read by sending 0xff
	uint8_t r = writeBits(m_bus,0xFF);
	log_dbug(TAG,"readByte() = 0x%02x",r);
	return r;
}


int OneWire::sendCommand(uint64_t id, uint8_t command)
{
	log_dbug(TAG,"sendCommand(" IDFMT ",%02x)",IDARG(id),command);
	setPower(false);
	resetBus();
	if (id) {
		uint8_t c = writeBits(m_bus,OW_MATCH_ROM);            // to a single device
		if (c != OW_MATCH_ROM) {
			log_warn(TAG,"match rom command failed");
			return 1;
		}
		for (unsigned i = 0; i < sizeof(id); ++i) {
			writeBits(m_bus,id&0xff);
			id >>= 8;
		}
	} else {
		writeBits(m_bus,OW_SKIP_ROM);            // to all devices
	}
	writeBits(m_bus,command);
	setPower(true);
	return 0;
}


#endif
