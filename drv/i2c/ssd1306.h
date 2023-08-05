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

#include "ssd130x.h"
//#include "fonts.h"
#include "i2cdrv.h"

class SSD1306 : public SSD130X, public I2CDevice
{
	public:
	enum hwcfg_t : uint8_t {
		hwc_iscan = 0x8,	// inverted scan
		hwc_altm = 0x10,	// alternating rows (non-sequential)
		hwc_rlmap = 0x20,	// right-to-left mapping
	};

	SSD1306(uint8_t bus, uint8_t addr);

	static SSD1306 *create(uint8_t bus, uint8_t addr);
	int init(uint8_t maxx, uint8_t maxy, uint8_t options);
	void drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg, int32_t bg) override;
//	int drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col) override;

	/*
	int write(const char *t, int n) override;
	int writeHex(uint8_t, bool) override;
	int clear() override;
	int clrEol() override;

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

	int setFont(unsigned f) override
	{
		m_font = (fontid_t) f;
		return 0;
	}
	*/

	void flush() override;
//	int setFont(const char *) override;
	int setBrightness(uint8_t contrast) override;
//	int setPos(uint16_t x, uint16_t y) override;
	int setInvert(bool inv) override;
	int setOn(bool on) override;
//	uint16_t numLines() const override;
//	uint16_t charsPerLine() const override;

//	void setPixel(uint16_t x, uint16_t y, int32_t col) override;

	const char *drvName() const
	{ return "ssd1306"; }

	static SSD1306 *getInstance()
	{ return Instance; }

	uint8_t maxBrightness() const override
	{ return 255; }

	private:
//	int drawBitmap_ssd1306(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data);
	int drawByte(uint8_t x, uint8_t y, uint8_t b);
	int drawBits(uint8_t x, uint8_t y, uint8_t b, uint8_t n);
//	void drawHLine(uint16_t x, uint16_t y, uint16_t n);
//	void drawVLine(uint8_t x, uint8_t y, uint8_t n);
	int drawChar(char c);
	int readByte(uint8_t x, uint8_t y, uint8_t *b);
	int drawMasked(uint8_t x, uint8_t y, uint8_t b, uint8_t m);
//	uint8_t fontHeight() const;

	static SSD1306 *Instance;
//	uint8_t m_maxx = 0, m_maxy = 0, m_posx = 0, m_posy = 0;
//	uint8_t *m_disp = 0;
//	uint8_t m_dirty = 0xff;
//	fontid_t m_font = font_native;
};

unsigned ssd1306_scan(uint8_t bus);

#endif

