/*
 *  Copyright (C) 2023-2024, Thomas Maier-Komor
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
#include "event.h"
#include "i2cdrv.h"

struct OPT3001 : public I2CDevice
{
	static OPT3001 *create(unsigned bus, unsigned addr);

	const char *drvName() const override
	{ return "opt3001"; }

	void attach(EnvObject *root) override;
	void addIntr(uint8_t intr) override;
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(Terminal &term, int argc, const char **args) override;
#endif

	private:
	typedef enum mode_e { mode_off = 0, mode_single = 1, mode_cont = 2 } mode_t;
	OPT3001(unsigned bus, unsigned addr);
	static void read(void *arg);
	static void single(void *arg);
	static void cont(void *arg);
	static void stop(void *arg);
	static unsigned cyclic(void *arg);
	int updateConfig(uint16_t);
	int init();
	int read(bool force = false);
	void setMode(mode_t m);

	EnvNumber m_lum;
	event_t m_isrev = 0;
	uint16_t m_cfg = 0;
};

#endif
