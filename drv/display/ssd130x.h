/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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
	: MatrixDisplay(cs_mono)
	{ }

	~SSD130X();
	void clear();
	uint16_t fontHeight() const;
//	int clrEol();
	uint16_t charsPerLine() const;
	uint16_t numLines() const;
	int setFont(unsigned) override;
	int setFont(const char *fn) override;
//	void drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg, int32_t bg) override;
	void drawHLine(uint16_t x, uint16_t y, uint16_t n, int32_t col = -1) override;
	void drawVLine(uint16_t x, uint16_t y, uint16_t n, int32_t col = -1) override;
//	void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col = -1) override;
	void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col = -1) override;
	void setPixel(uint16_t x, uint16_t y, int32_t col = -1) override;
	int writeHex(uint8_t h, bool comma);
//	int setPos(uint16_t x, uint16_t y);
	//void write(const char *text, int len);

	bool hasAlpha() const override
	{ return true; }

	bool hasChar(char) const override
	{ return true; }

	protected:
	void pClrPixel(uint16_t x, uint16_t y);
	void pSetPixel(uint16_t x, uint16_t y);
	int clearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
	int readByte(uint8_t x, uint8_t y, uint8_t *b);
	int drawMasked(uint8_t x, uint8_t y, uint8_t b, uint8_t m);
	int drawBits(uint8_t x, uint8_t y, uint8_t b, uint8_t n);
	int drawByte(uint8_t x, uint8_t y, uint8_t b);
	void drawChar(char c);
	unsigned drawChar(uint16_t x, uint16_t y, char c, int32_t fg, int32_t bg) override;
	void drawBitmapNative(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data);

	uint8_t *m_disp = 0;
	uint8_t m_dirty = 0xff;
//	fontid_t m_font = font_native;
};



#endif

