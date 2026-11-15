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

#ifndef HUSB238_H
#define HUSB238_H

#include "i2cdrv.h"
#include "env.h"

class HUSB238 : public I2CDevice
{
	public:
	static HUSB238 *create(uint8_t port, uint8_t addr);

	void attach(class EnvObject *) override;
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	HUSB238(uint8_t port, uint8_t addr);
	static unsigned cyclic(void *);
	unsigned pcyclic();
	const char *readReg(uint8_t reg, uint8_t &val);
	void printCapabilities(class Terminal &term);

	private:
	EnvNumber m_volt, m_curr;
};


#endif
