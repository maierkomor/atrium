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

#include "actions.h"
#include "dataflow.h"
#include "ds18b20.h"
#include "event.h"
#include "log.h"
#include "onewire.h"
#include "ujson.h"

#include <string.h>

#define DS18B20_CONVERT	0x44
#define DS18B20_READ	0xbe


static char TAG[] = "DS18B20";
static uint8_t NumDev = 0;


static void ds18b20_convert(void *arg)
{
	DS18B20 *o = (DS18B20 *)arg;
	OneWire::getInstance()->sendCommand(o->getId(),DS18B20_CONVERT);
}



static void ds18b20_read(void *arg)
{
	DS18B20 *o = (DS18B20 *)arg;
	o->read();
}


DS18B20::DS18B20(uint64_t id, const char *name)
: OwDevice(id,name)
{
	++NumDev;
	action_add(concat(name,"!convert"),ds18b20_convert,this,"");
	action_add(concat(name,"!read"),ds18b20_read,this,"");
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


void DS18B20::read()
{
	OneWire *ow = OneWire::getInstance();
	ow->sendCommand(getId(),DS18B20_READ);	// read scratchpad
	uint8_t sp[9];
	for (int i = 0; i < sizeof(sp); ++i)
		sp[i] = ow->readByte();
	m_value = (float)(((uint16_t)sp[1] << 8) | (uint16_t)sp[0]);
	m_value *= 0.0625;
	if (m_json)
		m_json->set(m_value);
	log_info(TAG,"%s: %f",m_name,m_value);
}


void DS18B20::attach(JsonObject *o)
{
	if (m_json == 0)
		m_json = o->add(m_name,0.0f);
}

#endif // CONFIG_ONEWIRE
