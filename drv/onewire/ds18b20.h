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

#ifndef DS18B20_H
#define DS18B20_H

#include "owdevice.h"


struct DS18B20 : public OwDevice
{
	public:
	static int create(uint64_t id, const char *name = 0);

	const char *deviceType() const override
	{ return "DS18B20"; }

	void attach(JsonObject *);
	void read();

	private:
	explicit DS18B20(uint64_t id, const char *name);

	class JsonNumber *m_json = 0;
};


#endif
