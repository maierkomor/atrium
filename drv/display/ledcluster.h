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
	virtual int write(uint8_t v);
	virtual int write(uint8_t *d, unsigned n);
	
	virtual uint8_t maxDim() const;
	virtual uint8_t getDim() const
	{ return 0; }
	virtual int setOn(bool);
	virtual int setDim(uint8_t)
	{ return -1; }
	virtual int setPos(uint8_t x, uint8_t y)
	{ return -1; }
	virtual int clear()
	{ return -1; }
	virtual int setNumDigits(unsigned)
	{ return -1; }
	virtual const char *drvName() const
	{ return ""; }

	static LedCluster *getInstance()
	{ return Instance; }

	LedCluster *m_next;
	static LedCluster *Instance;

	protected:
	LedCluster();
};


#endif
