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

#include "display.h"

class HT16K33 : public LedCluster
{
	public:
	static unsigned create(uint8_t);

	uint8_t getDim() const;
	uint8_t maxDim() const
	{ return 15; }
	int setDim(uint8_t);
	int setOn(bool);
	int setOffset(unsigned);
	int setNumDigits(unsigned);
//	int write1(bool, int off = -1);
	int write(uint8_t, int off = -1);
	int write(uint8_t *d, unsigned n, int off = -1);
//	int write16(uint16_t, int off = -1);
	void clear();

	int setPos(unsigned x, unsigned y);
	int write(char);
	int write(const char *);
	int init();
	const char *drvName() const;
	int setBlink(uint8_t);

	private:
	HT16K33(uint8_t bus, uint8_t addr)
	: m_bus(bus)
	, m_addr(addr)
	{ }

	~HT16K33() = default;

	uint8_t m_bus;
	uint8_t m_addr;
//	uint8_t m_data[16];
	uint8_t m_pos = 0;
	uint8_t m_dim = 0;
};


#endif
