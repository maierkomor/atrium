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

#ifndef SSD1306_H
#define SSD1306_H

#include "display.h"
#include "fonts_ssd1306.h"
#include "i2cdrv.h"

class SSD1306 : public DotMatrix, public I2CDevice
{
	public:
	enum hwcfg_t : uint8_t {
		hwc_iscan = 0x8,	// inverted scan
		hwc_altm = 0x10,	// alternating rows (non-sequential)
		hwc_rlmap = 0x20,	// right-to-left mapping
	};

	SSD1306(uint8_t bus, uint8_t addr);
	~SSD1306();

	static SSD1306 *create(uint8_t bus, uint8_t addr);
	int init(uint8_t maxx, uint8_t maxy, uint8_t options);
	int drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data) override;
	int drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;
	int clearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;

	int write(const char *t, int n) override;
	int writeHex(uint8_t, bool) override;
	int clear() override;
	int clrEol() override;
	int sync() override;

	uint16_t maxX() const override
	{ return m_maxx; }

	uint16_t maxY() const override
	{ return m_maxy; }

	int setXY(uint16_t x, uint16_t y) override
	{
		if (x >= m_maxx)
			return -1;
		if (y >= m_maxy)
			return -1;
		m_posx = x;
		m_posy = y;
		return 0;
	}

	int setFont(int f) override
	{
		m_font = (fontid_t) f;
		return 0;
	}

	int setFont(const char *) override;
	int setContrast(uint8_t contrast) override;
	int setPos(uint16_t x, uint16_t y) override;
	int setInvert(bool inv) override;
	int setOn(bool on) override;
	const char *drvName() const
	{ return "ssd1306"; }

	bool hasAlpha() const override
	{ return true; }

	bool hasChar(char) const override
	{ return true; }

	uint8_t numLines() const override;
	uint8_t charsPerLine() const override;

	static SSD1306 *getInstance()
	{ return Instance; }

	int setPixel(uint16_t x, uint16_t y) override;
	int clrPixel(uint16_t x, uint16_t y) override;

	private:
	int drawBitmap_ssd1306(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data);
	int drawByte(uint8_t x, uint8_t y, uint8_t b);
	int drawBits(uint8_t x, uint8_t y, uint8_t b, uint8_t n);
	void drawHLine(uint8_t x, uint8_t y, uint8_t n);
	void drawVLine(uint8_t x, uint8_t y, uint8_t n);
	int drawChar(char c);
	int readByte(uint8_t x, uint8_t y, uint8_t *b);
	int drawMasked(uint8_t x, uint8_t y, uint8_t b, uint8_t m);
	uint8_t fontHeight() const;

	static SSD1306 *Instance;
	uint8_t *m_disp = 0;
	uint8_t m_maxx = 0, m_maxy = 0, m_posx = 0, m_posy = 0, m_dirty = 0xff;
	fontid_t m_font = font_native;
};


#endif

