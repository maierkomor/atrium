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

#ifndef PCF8574_H
#define PCF8574_H

#include "i2cdrv.h"

class PCF8574 : public I2CDevice
{
	public:
	static unsigned create(uint8_t);

	int setGpio(bool v,unsigned off);
	int write(uint8_t);
	int write(uint8_t *v, unsigned n);
	uint8_t read();
	void clear();

	int init();
	const char *drvName() const;

	static PCF8574 *getInstance()
	{ return Instance; }

	private:
	PCF8574(uint8_t bus, uint8_t addr)
	: I2CDevice(bus,addr,drvName())
	{ }

	~PCF8574() = default;

	static PCF8574 *Instance;
	uint8_t m_data;
};


#endif

