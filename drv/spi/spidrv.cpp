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

#ifdef CONFIG_SPI

#include "log.h"
#include "spidrv.h"

#define TAG MODULE_SPI


static SemaphoreHandle_t Mtx = 0;
SpiDevice *SpiDevice::m_first;


SpiDevice::SpiDevice(const char *name, uint8_t cs)
: m_cs(cs)
{
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	strcpy(m_name,name);
	bool x = hasInstance(name);
	log_info(TAG,"init %s",name);
//	Lock lock(Mtx);
	m_next = m_first;
	m_first = this;
	if (x)
		updateNames(name);
}

void SpiDevice::attach(class EnvObject *)
{

}

void SpiDevice::setName(const char *n)
{
	strncpy(m_name,n,sizeof(m_name)-1);
	m_name[sizeof(m_name)-1] = 0;
}


bool SpiDevice::hasInstance(const char *d)
{
	size_t l = strlen(d);
	SpiDevice *s = m_first;
	while (s) {
		if (0 == strncmp(s->m_name,d,l))
			return true;
		s = s->m_next;
	}
	return false;
}


void SpiDevice::updateNames(const char *dev)
{
	// called from constructor: no virtual calls possible
	SpiDevice *s = m_first;
	while (s) {
		if (0 == strcmp(dev,s->m_name))
			s->updateName();
		s = s->m_next;
	}
}


void SpiDevice::updateName()
{
	// called from constructor: no virtual calls possible
	size_t off = strlen(m_name);
	int n = snprintf(m_name+off,sizeof(m_name)-off,"@%u",m_cs);
	if (n+off >= sizeof(m_name)) {
		log_error(TAG,"name truncated: %s",m_name);
		m_name[sizeof(m_name)-1] = 0;
	}
}



#endif
