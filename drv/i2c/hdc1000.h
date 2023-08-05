/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#ifndef HDC1000_H
#define HDC1000_H

#include <sdkconfig.h>
#include "env.h"
#include "i2cdrv.h"

class EnvNumber;

// supports also HDC1080
struct HDC1000 : public I2CDevice
{
	const char *drvName() const
	{ return m_drvname; }

	int init();
	void attach(class EnvObject *);
	unsigned cyclic();

	static HDC1000 *create(uint8_t bus, uint8_t addr, uint16_t id);
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	HDC1000(uint8_t port, uint8_t addr, const char *name);

	static void trigger(void *);
	static void trigger_humid(void *);
	static void trigger_temp(void *);
	bool status();
	bool sample();
	bool read();
	void handle_error();
	void setHeater(bool on);
	void setSingle(bool);
	void setTemp(uint8_t data[]);
	void setHumid(uint8_t data[]);

	const char *m_drvname;
	EnvNumber m_temp, m_humid;
	typedef enum { st_idle, st_readtemp, st_readhumid, st_readboth } state_t;
	typedef enum { sm_none, sm_temp, sm_humid, sm_seq, sm_both } sample_t;
	typedef enum { tres_14b = 0, tres_11b = (1<<10) } tres_t;
	typedef enum { hres_14b = 0, hres_11b = 0x100, hres_8b = 0x200 } hres_t;
	state_t m_state = st_idle;
	sample_t m_sample = sm_none;
	bool m_cfgsynced = false;	// has the configuration been written to the device?
	uint16_t m_cfg = 0x1000;	// reset value of device
};


#endif
