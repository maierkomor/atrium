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

#ifndef HD44780U_H
#define HD44780U_H

#include "display.h"

class PCF8574;

class HD44780U : public TextDisplay
{
	public:
	explicit HD44780U(PCF8574 *drv, uint8_t maxx, uint8_t maxy)
	: TextDisplay()
	, m_drv(drv)
	, m_maxx(maxx)
	, m_maxy(maxy)
	{ }
	
	bool hasAlpha() const override
	{ return true; }

	bool hasChar(char c) const override;
	int init();
	int write(const char *, int = -1) override;
	int writeHex(uint8_t h, bool c) override;
	int clear() override;
	int clrEol();
	int setBlink(bool) override;
	int setCursor(bool blink) override;
	int setDim(uint8_t) override;
	int setOn(bool) override;
	void setDisplay(bool on, bool cursor, bool blink);
	int setPos(uint16_t x, uint16_t y = 0) override;
	uint8_t maxDim() const override
	{ return 1; }
	uint8_t charsPerLine() const override
	{ return m_maxx; }
	uint8_t numLines() const override
	{ return m_maxy; }

	private:
	int writeCmd(uint8_t);
	int writeData(uint8_t);
	uint8_t readBusy();

	PCF8574 *m_drv = 0;
	uint32_t m_lcmd = 0;	// time-stamp of last command
	uint8_t m_disp = 0;
	bool m_posinv = false;
	uint8_t m_posx = 0, m_posy = 0;
	uint8_t m_maxx, m_maxy;
	char m_data[80];
};

#endif
