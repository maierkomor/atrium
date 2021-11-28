/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

#include "ds18b20.h"
#include "log.h"
#include "owdevice.h"
#include <stdlib.h>
#include <string.h>


#define TAG MODULE_OWB
OwDevice *OwDevice::First = 0;


OwDevice::OwDevice(uint64_t id, const char *name)
: m_id(id)
, m_next(First)
, m_name(name)
{
	First = this;
}


OwDevice *OwDevice::getDevice(uint64_t id)
{
	OwDevice *r = First;
	while (r && r->m_id != id)
		r = r->m_next;
	return r;
}

void OwDevice::setName(const char *name)
{
	m_name = name;
}


int OwDevice::create(uint64_t id, const char *name)
{
	log_dbug(TAG,"registering device " IDFMT,IDARG(id));
	if ((id & 0xff) == 0x28)	// a DS18B20
		return DS18B20::create(id,name);
	else
		return 1;
}
#endif
