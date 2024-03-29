/*
 *  Copyright (C) 2021-2024, Thomas Maier-Komor
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

#ifndef SSD130X_H
#define SSD130X_H

#include "display.h"

class SSD130X : public MatrixDisplay
{
	public:
	SSD130X()
	: MatrixDisplay(cs_mono,1/*FG:WHITE*/,0/*BG:BLACK*/)
	{ }

	~SSD130X();
	void clear();
	uint16_t numLines() const;
	void drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg = -1, int32_t bg = -1) override;
	unsigned drawChar(uint16_t x, uint16_t y, char c, int32_t fg, int32_t bg) override;
	void drawHLine(uint16_t x, uint16_t y, uint16_t n, int32_t col = -1) override;
	void drawVLine(uint16_t x, uint16_t y, uint16_t n, int32_t col = -1) override;
	int32_t getColor(color_t) const override;
	void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col = -1) override;
	void setPixel(uint16_t x, uint16_t y, int32_t col = -1) override;
	int setupOffScreen(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t bg) override;

	bool hasAlpha() const override
	{ return true; }

	bool hasChar(char) const override
	{ return true; }

	pxlfmt_t pixelFormat() const override
	{ return pxf_bytecolmjr; }

	protected:
	void pClrPixel(uint16_t x, uint16_t y);
	void pSetPixel(uint16_t x, uint16_t y);
	int clearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
	int readByte(uint8_t x, uint8_t y, uint8_t *b);
	int drawMasked(uint8_t x, uint8_t y, uint8_t b, uint8_t m);
	int drawBits(uint8_t x, uint8_t y, uint8_t b, uint8_t n);
	int drawByte(uint8_t x, uint8_t y, uint8_t b);

	uint8_t *m_disp = 0;
	uint8_t m_dirty = 0xff;
};



#endif

