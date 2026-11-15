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

#ifndef SHT4x_H
#define SHT4x_H

#include "i2cdrv.h"
#include "env.h"

#define SIZE_SERIAL 6

class EnvNumber;


struct SHT4x : public I2CDevice
{
	SHT4x(uint8_t port, uint8_t addr, const char *n = 0);

	static SHT4x *create(uint8_t bus, uint8_t addr);

	const char *drvName() const override;
	void attach(class EnvObject *) override;
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	int init();
	void reset();
	static unsigned cyclic(void *arg);
	unsigned cyclic();
	int send_cmd(uint8_t);
	void read_values();

	typedef enum {
		st_idle, st_meas_hp, st_meas_mp, st_meas_lp, st_read,
		st_heat_1, st_heat_2, st_heat_3, st_heat_4,  st_heat_5,  st_heat_6,  
		st_reset,
	} state_t;
	typedef enum {
		md_off, md_hp, md_mp, md_lp,
	} mode_t;
	EnvNumber m_temp, m_humid;
	uint8_t m_serial[SIZE_SERIAL];
	state_t m_state = st_reset;
	mode_t m_mode = md_mp;

};


unsigned sht4x_scan(uint8_t);


#endif
