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

#ifndef OPT3001_H
#define OPT3001_H

#include <sdkconfig.h>
#include "env.h"
#include "i2cdrv.h"

struct OPT3001 : public I2CDevice
{
	static unsigned scan(unsigned bus);

	void attach(EnvObject *root) override;
	void addIntr(uint8_t intr) override;
	unsigned cyclic(void *arg);
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(Terminal &term, int argc, const char **args) override;
#endif
	int read();

	private:
	OPT3001(unsigned bus, unsigned addr);
	void sample(void *arg);

	EnvNumber m_lux;
};

#endif
