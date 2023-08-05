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

#ifndef CCS811_H
#define CCS811_H

#include "i2cdrv.h"

class EnvNumber;


class CCS811B : public I2CDevice
{
	public:
	CCS811B(uint8_t, uint8_t);

	const char *drvName() const
	{ return "ccs811b"; }

	int init();
	void attach(class EnvObject *);
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	private:
	static unsigned cyclic(void *);
	uint8_t status();
	uint8_t error();
	unsigned updateHumidity();
	unsigned read();

	typedef enum { st_idle, st_sample, st_measure, st_update, st_read } state_t;
	EnvNumber *m_tvoc = 0, *m_co2 = 0;
	EnvObject *m_root = 0;
	uint16_t m_temp = 0, m_humid = 0, m_cnt = 0;
	state_t m_state;
	uint8_t m_err = 0;

};


#endif
