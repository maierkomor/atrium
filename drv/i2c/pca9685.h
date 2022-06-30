/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#ifndef PCA9685_H
#define PCA9685_H

#include "i2cdrv.h"


class PCA9685 : public I2CDevice
{
	public:
	static PCA9685 *create(uint8_t bus, uint8_t addr, bool invrt, bool outdrv, bool xclk = false);

	const char *drvName() const override
	{ return "pca9685"; }

	void attach(class EnvObject *) override;

	int setPrescale(uint8_t ps);
	int setCh(int8_t led, uint16_t duty, uint16_t off = 0);	// duty=0..4096

#ifdef CONFIG_I2C_XCMD
	int exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	PCA9685 *getNext() const
	{ return m_next; }

	private:
	PCA9685(uint8_t bus, uint8_t addr);
	int setAll(uint16_t off, uint16_t on);
	
	PCA9685 *m_next;
};

#endif
