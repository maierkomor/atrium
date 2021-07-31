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

class DotMatrix;
class SegmentDisplay;

struct TextDisplay
{
	virtual DotMatrix *toDotMatrix()
	{ return 0; }

	virtual SegmentDisplay *toSegmentDisplay()
	{ return 0; }

	virtual int clear()
	{ return -1; }

	virtual bool hasChar(char c) const
	{ return false; }

	virtual int setBlink(bool)
	{ return -1; }

	virtual int setCursor(bool)
	{ return -1; }

	virtual int setPos(uint8_t x, uint8_t y = 0)
	{ return -1; }

	virtual uint8_t charsPerLine() const
	{ return 0; }

	virtual uint8_t numLines() const
	{ return 0; }

	virtual int setOn(bool on)
	{ return -1; }

	static TextDisplay *getFirst()
	{ return Instance; }

	TextDisplay *getNext() const
	{ return m_next; }

	virtual int writeBin(uint8_t)
	{ return -1; }

	virtual int writeHex(uint8_t h, bool comma = false)
	{ return -1; }

	virtual int write(const char *txt, int n = -1)
	{ return -1; }

	virtual bool hasAlpha() const
	{ return false; }

	virtual uint8_t maxDim() const
	{ return 1; }

	virtual int getDim() const
	{ return -1; }

	virtual int setDim(uint8_t)
	{ return -1; }

	virtual int sync()
	{ return -1; }

	virtual int clrEol()
	{ return -1; }

	virtual int setPixel(uint16_t x, uint16_t y)
	{ return -1; }

	virtual int clrPixel(uint16_t x, uint16_t y)
	{ return -1; }

	virtual int setFont(int)
	{ return -1; }

	void initOK();

	private:
	static TextDisplay *Instance;
	TextDisplay *m_next = 0;
};


struct DotMatrix : public TextDisplay
{
	DotMatrix *toDotMatrix() override
	{ return this; }

	virtual uint16_t maxX() const
	{ return 0; }

	virtual uint16_t maxY() const
	{ return 0; }

	virtual int setXY(uint16_t x, uint16_t y)
	{ return -1; }

	virtual int clearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
	{ return -1; }

	virtual int drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
	{ return -1; }

	virtual int drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data)
	{ return -1; }

	virtual int drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
	{ return -1; }

	virtual int setInvert(bool)
	{ return -1; }

	virtual int setContrast(uint8_t contrast)
	{ return -1; }

	virtual int sync()
	{ return -1; }
};


struct TftDisplay : public DotMatrix
{
	virtual int setColor(uint32_t)
	{ return -1; }

};


struct SegmentDisplay : public TextDisplay
{
	typedef enum { e_raw = 0, e_seg7 = 1, e_seg14 = 2 } addrmode_t;

	SegmentDisplay(LedCluster *, addrmode_t m, uint8_t maxx, uint8_t maxy = 1);

	SegmentDisplay *toSegmentDisplay() override
	{ return this; }

	int writeHex(uint8_t d, bool comma = false);
	int writeChar(char, bool = false);
	int writeBin(uint8_t);
	int write(const char *txt, int n = -1) override;
	int clear() override
	{ return m_drv->clear(); }

	int setOn(bool on) override
	{ return m_drv->setOn(on); }

	int getDim() const override
	{ return m_drv->getDim(); }

	int setDim(uint8_t d) override
	{ return m_drv->setDim(d); }

	// X=0,Y=0: upper left
	int setPos(uint8_t x, uint8_t y = 0) override;

	// characters per line
	uint8_t charsPerLine() const override
	{ return m_maxx; }

	// number of lines
	uint8_t numLines() const override
	{ return m_maxy; }

	// maximum brightness
	uint8_t maxDim() const override
	{ return m_drv->maxDim(); }

	bool hasAlpha() const override
	{ return (m_addrmode == e_seg14); }

	bool hasChar(char) const override;

	protected:
	static uint16_t char2seg7(char c);
	static uint16_t char2seg14(char c);

	LedCluster *m_drv;
	addrmode_t m_addrmode;
	uint8_t m_maxx, m_maxy;
	uint16_t m_pos = 0;
};


#endif
