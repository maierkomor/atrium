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

#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <driver/gpio.h>
#include <esp_timer.h>

#include <vector>


class OneWire
{
	public:
	OneWire(unsigned bus, bool pullup);

	int sendCommand(uint64_t id, uint8_t command);
	uint8_t readByte();
	uint8_t writeByte(uint8_t);
	int resetBus();
	int scanBus();
	int readRom();
	unsigned xmitBit(uint8_t);

	static OneWire *getInstance()
	{ return Instance; }

	private:
	int addDevice(uint64_t);
	int searchRom(uint64_t &id, std::vector<uint64_t> &collisions);
	gpio_num_t m_bus, m_pwr;
	static OneWire *Instance;
};


#endif
