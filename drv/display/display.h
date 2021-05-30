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

#ifndef DISPLAY_H
#define DISPLAY_H

#include "ledcluster.h"
#include <stdint.h>


struct SegmentDisplay
{
	typedef enum { e_raw = 0, e_seg7 = 1, e_seg14 = 2 } addrmode_t;

	SegmentDisplay(LedCluster *, addrmode_t m, uint8_t maxx, uint8_t maxy = 1);

	int writeHex(uint8_t d, bool comma = false);
	int writeChar(char, bool = false);
	int writeBin(uint8_t);
	int write(const char *txt);
	void clear()
	{ return m_drv->clear(); }

	int setOn(bool on)
	{ return m_drv->setOn(on); }

	uint8_t getDim() const
	{ return m_drv->getDim(); }

	void setDim(uint8_t d)
	{ m_drv->setDim(d); }

	// X=0,Y=0: upper left
	int setPos(uint8_t x, uint8_t y);

	// characters per line
	uint8_t maxX() const
	{ return m_maxx; }

	// number of lines
	uint8_t maxY() const
	{ return m_maxy; }

	// maximum brightness
	uint8_t maxDim() const
	{ return m_drv->maxDim(); }

	bool hasAlpha() const
	{ return (m_addrmode == e_seg14); }

	SegmentDisplay *getNext() const
	{ return m_next; }

	static SegmentDisplay *getInstance()
	{ return Instance; }

	protected:
	static uint16_t char2seg7(char c);
	static uint16_t char2seg14(char c);

	SegmentDisplay *m_next;
	LedCluster *m_drv;
	addrmode_t m_addrmode;
	uint8_t m_maxx, m_maxy;
	uint16_t m_pos = 0;

	static SegmentDisplay *Instance;
};


#endif
