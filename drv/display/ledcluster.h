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

#ifndef LEDCLUSTER_H
#define LEDCLUSTER_H

#include <stdint.h>

class LedCluster
{
	public:
	// off single LED offset
//	virtual int write1(bool v, int off = -1) = 0;

	// off = address of 8-bit segment
	virtual int write(uint8_t v, int off = -1);
	virtual int write(uint8_t *d, unsigned n, int off = -1);
	
	// off = address of 16-bit segment
//	virtual int write16(uint16_t v, int off = -1) = 0;

	virtual uint8_t maxDim() const;
	virtual uint8_t getDim() const
	{ return 0; }
	virtual int setOn(bool);
	virtual int setDim(uint8_t)
	{ return -1; }
	virtual int setOffset(unsigned)
	{ return -1; }
	virtual void clear()
	{ }
	virtual int setNumDigits(unsigned)
	{ return -1; }

	static LedCluster *getInstance()
	{ return Instance; }

	LedCluster *m_next;
	static LedCluster *Instance;

	protected:
	LedCluster();
};


#endif
