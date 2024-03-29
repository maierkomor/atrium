/*
 *  Copyright (C) 2024, Thomas Maier-Komor
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

#ifndef AHTXX_H
#define AHTXX_H

#include "env.h"
#include "i2cdrv.h"


struct AHTXX: public I2CDevice
{
	typedef enum ahtdev_e { aht10, aht20, aht21, aht30 } ahtdev_t;
	AHTXX(uint8_t bus, uint8_t addr, ahtdev_t d);

	void attach(class EnvObject *) override;
	const char *drvName() const override;
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	float calc_press(int32_t adc_P, int32_t t_fine);
	int32_t calc_tfine(uint8_t *);
	unsigned cyclic();
	static void trigger(void *);
	int getStatus();
	void trigger();
	int read();
	int reset(uint8_t bus, ahtdev_t d);
	static unsigned cyclic(void *);
	static void sample(void *);
	static int init(uint8_t, ahtdev_t);

	friend int aht_scan(uint8_t,ahtdev_t);

	typedef enum { st_idle, st_trigger, st_read } state_t;
	EnvNumber m_temp, m_humid;
	ahtdev_t m_dev;
	state_t m_state = st_idle;
};


int aht_scan(uint8_t, AHTXX::ahtdev_t);


#endif
