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

#ifndef SI7021_H
#define SI7021_H

#include "i2cdrv.h"

class EnvNumber;


struct SI7021 : public I2CDevice
{
	SI7021(uint8_t port, const char *typ, bool combined);

	const char *drvName() const override
	{ return m_type; }

//	int init() override;
	void attach(class EnvObject *) override;
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif
	static SI7021 *create(uint8_t bus, uint8_t addr);

	protected:
	static unsigned cyclic(void *);
	static void triggerh(void *arg);
	static void triggert(void *arg);
	int setHeater(bool on);
	int reset();

	private:
	const char *m_type;
	EnvNumber *m_humid = 0, *m_temp = 0;
	typedef enum state_e { st_off, st_triggerh, st_readh, st_triggert, st_readt, st_cont } state_t;
	state_t m_st = st_off;
	uint8_t m_uc1, m_mode;
	bool m_combined;
};

#endif
