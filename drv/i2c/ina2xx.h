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

#ifndef INA2XX_H
#define INA2XX_H

#include "i2cdrv.h"

class EnvNumber;


struct INA219 : public I2CDevice
{
	static INA219 *create(uint8_t port, uint8_t addr);

	const char *drvName() const override
	{ return "ina219"; }

//	int init() override;
	void attach(class EnvObject *) override;
#ifdef CONFIG_I2C_XCMD
	int exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	INA219(uint8_t port, uint8_t addr);
	static unsigned cyclic(void *);
	static void trigger(void *arg);
	void updateDelay();

	private:
	EnvNumber *m_volt = 0, *m_amp = 0, *m_shunt;
	uint16_t m_conf = 0;
	typedef enum state_e { st_off, st_trigger, st_read, st_cont } state_t;
	state_t m_st;
	uint8_t m_delay = 0, m_badc = 0, m_sadc = 0;
};

/* TODO
struct INA226 : public I2CDevice
{
	INA226(uint8_t port, uint8_t addr, const char *n = 0);

	const char *drvName() const
	{ return "ina226"; }

	int init();
	void attach(class EnvObject *);
#ifdef CONFIG_I2C_XCMD
	int exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:

};
*/

#endif
