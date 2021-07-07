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

#ifndef HT16K33_H
#define HT16K33_H

#include "ledcluster.h"

class HT16K33 : public LedCluster
{
	public:
	static unsigned create(uint8_t);

	int setOn(bool);
	int setOffset(unsigned);
	int setNumDigits(unsigned);
	int write(uint8_t) override;
	int write(uint8_t *d, unsigned n) override;
	int clear();

	int setPos(uint8_t x, uint8_t y) override;
//	int write(char);
//	int write(const char *);
	int init();
	const char *drvName() const override;
	int setBlink(uint8_t);

	private:
	HT16K33(uint8_t bus, uint8_t addr)
	: m_bus(bus)
	, m_addr(addr)
	{ }

	~HT16K33() = default;

	uint8_t m_bus;
	uint8_t m_addr;
	uint8_t m_data[16];
	uint8_t m_pos = 0;
	uint8_t m_digits = 0;
};


#endif
