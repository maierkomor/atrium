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

#ifndef OWDEVICE_H
#define OWDEVICE_H

#include <cstdint>

#define IDFMT "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x"
#define IDARG(x) (unsigned)(x&0xff),(unsigned)((x>>8)&0xff),(unsigned)((x>>16)&0xff),(unsigned)((x>>24)&0xff),(unsigned)((x>>32)&0xff),(unsigned)((x>>40)&0xff),(unsigned)((x>>48)&0xff),(unsigned)((x>>56)&0xff)

class EnvObject;

class OwDevice
{
	public:
	explicit OwDevice(uint64_t id, const char *name = 0);

	virtual const char *deviceType() const
	{ return "unknown"; }

	uint64_t getId() const
	{ return m_id; }

	uint8_t getType() const
	{ return m_id&0xff; }

	uint8_t crc() const
	{ return m_id>>56; }

	void setName(const char *name);

	const char *getName() const
	{ return m_name ? m_name : ""; }

	static OwDevice *firstDevice()
	{ return First; }

	OwDevice *getNext() const
	{ return m_next; }

	virtual void attach(EnvObject *)
	{ }

	static OwDevice *getDevice(uint64_t id);

	static int create(uint64_t id, const char *name = 0);

	protected:
	~OwDevice();

	uint64_t m_id;
	OwDevice *m_next = 0;
	const char *m_name = 0;
	static OwDevice *First;

	private:
	OwDevice(const OwDevice &);
	OwDevice &operator = (const OwDevice &);

};


#endif
