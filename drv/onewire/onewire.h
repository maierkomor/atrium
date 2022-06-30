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

#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <esp_timer.h>

#include <vector>

#include "xio.h"

class OneWire
{
	public:
	static OneWire *create(unsigned bus, bool pullup, int8_t pwr = -1);

	int sendCommand(uint64_t id, uint8_t command);
	uint8_t readByte();
	uint8_t writeByte(uint8_t);
	int resetBus();
	int scanBus();
	int readRom();
	unsigned xmitBit(uint8_t);
	void setPower(bool);

	static OneWire *getInstance()
	{ return Instance; }

	static uint8_t crc8(const uint8_t *in, size_t len);

	private:
	OneWire(xio_t bus, xio_t pwr);
	int addDevice(uint64_t);
	int searchRom(uint64_t &id, std::vector<uint64_t> &collisions);

	xio_t m_bus, m_pwr;
	bool m_pwron = false;

	static OneWire *Instance;
};


#endif
