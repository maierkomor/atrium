/*
 *  Copyright (C) 2021-2025, Thomas Maier-Komor
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

#ifndef HT16K33_H
#define HT16K33_H

#include "ledcluster.h"
#include "i2cdrv.h"

class HT16K33 : public LedCluster, public I2CDevice
{
	public:
	static void create(uint8_t,uint8_t);

	int cfgIntr(bool on, bool act_high = false);
	int setPower(bool);
	int setDisplay(bool);
	int setOffset(unsigned);
	int setNumDigits(unsigned);
	int write(uint8_t) override;
	int write(uint8_t *d, unsigned n) override;
	int writeHex(char *h);
	int clear();

	int setDim(uint8_t) override;
	int setPos(uint8_t x, uint8_t y) override;
	int init();
	const char *drvName() const override;
	int setBlink(uint8_t);

#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	HT16K33 *next() const
	{ return m_next; }

	private:
	HT16K33(uint8_t bus, uint8_t addr)
	: LedCluster()
	, I2CDevice(bus,addr,"ht16k33")
	{ }

	~HT16K33() = default;

	HT16K33 *m_next = 0;
	uint8_t m_data[16];
	uint8_t m_pos = 0;
	uint8_t m_digits = 8;
	bool m_disp = 0;	// bit0: off/on, bits2..1: blink 0..3
};


#endif
