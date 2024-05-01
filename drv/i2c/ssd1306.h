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

#ifndef SSD1306_H
#define SSD1306_H

#include "ssd130x.h"
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
	int init(uint16_t maxx, uint16_t maxy, uint8_t options) override;

	void flush() override;
	int setBrightness(uint8_t contrast) override;
	int setInvert(bool inv) override;
	int setOn(bool on) override;

	const char *drvName() const
	{ return "ssd1306"; }

	static SSD1306 *getInstance()
	{ return Instance; }

	uint8_t maxBrightness() const override
	{ return 255; }

	private:
	static SSD1306 *Instance;
};

unsigned ssd1306_scan(uint8_t bus);

#endif

