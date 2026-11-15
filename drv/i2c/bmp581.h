/*
 *  Copyright (C) 2026, Thomas Maier-Komor
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

#ifndef BMP581_H
#define BMP581_H

#include "env.h"
#include "event.h"
#include "i2cdrv.h"


struct BMP581 : public I2CDevice
{
	BMP581(uint8_t port, uint8_t addr, const char *n = 0);

	const char *drvName() const
	{ return "bmp581"; }

	void addIntr(uint8_t intr) override;
	void attach(class EnvObject *);
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	int init();
	int read_data();
	static unsigned cyclic(void *);
	unsigned cyclic();

	EnvNumber m_temp, m_press;
	uint16_t m_itv = 1000, m_restart = 0;
	// oversampling: after reset temperature x1, pressure off
	uint8_t m_temp_os = 1, m_press_os = 0;
};


unsigned bmp581_scan(uint8_t port);

#endif
