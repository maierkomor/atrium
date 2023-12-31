/*
 *  Copyright (C) 2020-2022, Thomas Maier-Komor
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

#include "actions.h"
#include "cyclic.h"
#include "ds18b20.h"
#include "env.h"
#include "event.h"
#include "log.h"
#include "onewire.h"
#include "stream.h"

#include <string.h>

#define DS18B20_CONVERT	0x44
#define DS18B20_READ	0xbe
#define DS18B20_WRITE	0x4e


#define TAG MODULE_DS18B20
static uint8_t NumDev = 0;
static const uint16_t ConvTime[] = { 95, 190, 380, 760 };


void DS18B20::sample(void *arg)
{
	DS18B20 *o = (DS18B20 *)arg;
	if (o->m_st == st_idle)
		o->m_st = st_sample;
}


void DS18B20::set_res9b(void *arg)
{
	DS18B20 *o = (DS18B20 *)arg;
	if (o->m_st == st_idle)
		o->m_st = st_set9b;
}


void DS18B20::set_res10b(void *arg)
{
	DS18B20 *o = (DS18B20 *)arg;
	if (o->m_st == st_idle)
		o->m_st = st_set10b;
}


void DS18B20::set_res11b(void *arg)
{
	DS18B20 *o = (DS18B20 *)arg;
	if (o->m_st == st_idle)
		o->m_st = st_set11b;
}


void DS18B20::set_res12b(void *arg)
{
	DS18B20 *o = (DS18B20 *)arg;
	if (o->m_st == st_idle)
		o->m_st = st_set12b;
}


DS18B20::DS18B20(uint64_t id, const char *name)
: OwDevice(id,name)
{
	++NumDev;
	OneWire *ow = OneWire::getInstance();
	bool ready = false;
	if (0 == ow->sendCommand(getId(),DS18B20_READ)) {	// read scratchpad
		uint8_t sp[5];
		for (int i = 0; i < sizeof(sp); ++i)
			sp[i] = ow->readByte();
		if ((sp[4] & 0x9f) == 0x1f) {
			m_res = (res_t)((sp[4] >> 5 ) & 3);
			log_dbug(TAG,"resolution is %ubits",m_res+9);
			ready = true;
		}
	}
	if (ready) {
		action_add(concat(name,"!sample"),sample,this,"trigger DS18B20 convertion/sampling");
		action_add(concat(name,"!setres9b"),set_res9b,this,"set conversion resolution to 9bit");
		action_add(concat(name,"!setres10b"),set_res10b,this,"set conversion resolution to 10bit");
		action_add(concat(name,"!setres11b"),set_res11b,this,"set conversion resolution to 11bit");
		action_add(concat(name,"!setres12b"),set_res12b,this,"set conversion resolution to 12bit");
	} else {
		log_warn(TAG,"device %s is not ready",name);
	}
}


int DS18B20::create(uint64_t id, const char *n)
{
	if ((id & 0xff) != 0x28)
		return 1;
	log_dbug(TAG,"device id " IDFMT,IDARG(id));
	if (n == 0) {
		char name[32];
		sprintf(name,"ds18b20_%u",NumDev);
		n = strdup(name);
	}
	new DS18B20(id,n);
	return 0;
}


unsigned DS18B20::cyclic(void *arg)
{
	DS18B20 *dev = (DS18B20 *) arg;
	switch (dev->m_st) {
	case st_idle:
		break;
	case st_sample:
		{
			OneWire *ow = OneWire::getInstance();
			ow->sendCommand(dev->getId(),DS18B20_CONVERT);
			dev->m_st = st_read;
		}
		return ConvTime[dev->m_res];
	case st_read:
		dev->read();
		break;
	case st_set9b:
		dev->set_resolution(res_9b);
		break;
	case st_set10b:
		dev->set_resolution(res_10b);
		break;
	case st_set11b:
		dev->set_resolution(res_11b);
		break;
	case st_set12b:
		dev->set_resolution(res_12b);
		break;
	default:
		;
	}
	dev->m_st = st_idle;
	return 50;
}


void DS18B20::read()
{
	log_dbug(TAG,"%s read",m_name);
	OneWire *ow = OneWire::getInstance();
	bool reset = true;
	if (0 == ow->sendCommand(getId(),DS18B20_READ)) {	// read scratchpad
		uint8_t sp[9];
		for (int i = 0; i < sizeof(sp); ++i)
			sp[i] = ow->readByte();
		uint8_t crc = OneWire::crc8(sp,8);
		if (crc != sp[8]) {
			log_warn(TAG,"read CRC: expected 0x%02x, got 0x%02x",sp[8],crc);
		} else {
			log_dbug(TAG,"CRC ok");
		}
		uint16_t v = ((uint16_t)(sp[1] & 0x7f) << 8) | (uint16_t)sp[0];
		if (v != 0x7fff) {
			reset = false;
			float f = v;
			if (sp[1] & 0xf8)
				f *= -1;
			f *= 0.0625;
			if (m_json)
				m_json->set(f);
			char tmp[8];
			float_to_str(tmp,f);
			log_dbug(TAG,"%s: %s",m_name,tmp);
		}
	}
	if (reset && m_json) {
		log_dbug(TAG,"%s: NAN",m_name);
		m_json->set(NAN);
	}
}


void DS18B20::set_resolution(res_t r)
{
	OneWire *ow = OneWire::getInstance();
	ow->sendCommand(getId(),DS18B20_WRITE);
	ow->writeByte(0);
	ow->writeByte(0);
	ow->writeByte(r << 5|0x1f);
	m_res = r;
}


void DS18B20::attach(EnvObject *o)
{
	cyclic_add_task(m_name,cyclic,this,0);
	if (m_json == 0)
		m_json = o->add(m_name,NAN,"\u00b0C");
}

#endif // CONFIG_ONEWIRE
