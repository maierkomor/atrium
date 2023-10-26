/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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

#ifndef SH1106_H
#define SH1106_H

#include "ssd130x.h"
#include "i2cdrv.h"

class SH1106 : public SSD130X, public I2CDevice
{
	public:
	enum hwcfg_t : uint8_t {
		hwc_iscan = 0x8,	// inverted scan
		hwc_altm = 0x10,	// alternating rows (non-sequential)
		hwc_rlmap = 0x20,	// right-to-left mapping
	};

	SH1106(uint8_t bus, uint8_t addr);

	static SH1106 *create(uint8_t bus, uint8_t addr);
	int init(uint8_t maxx, uint8_t maxy, uint8_t options);
	void drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg, int32_t bg) override;

	void flush() override;
	int setBrightness(uint8_t contrast) override;
	int setInvert(bool inv) override;
	int setOn(bool on) override;

	const char *drvName() const
	{ return "sh1106"; }

	static SH1106 *getInstance()
	{ return Instance; }

	uint8_t maxBrightness() const override
	{ return 255; }

	private:
	int drawByte(uint8_t x, uint8_t y, uint8_t b);
	int drawBits(uint8_t x, uint8_t y, uint8_t b, uint8_t n);
	int drawChar(char c);
	int readByte(uint8_t x, uint8_t y, uint8_t *b);
	int drawMasked(uint8_t x, uint8_t y, uint8_t b, uint8_t m);
	int xmitCmd(uint8_t cmd);
	void xmitCmds(uint8_t *cmd, unsigned n);

	static SH1106 *Instance;
};

unsigned sh1106_scan(uint8_t bus);

#endif

