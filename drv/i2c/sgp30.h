/*
 *  Copyright (C) 2021, Thomas Maier-Komor
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

#ifndef SGP30_H
#define SGP30_H

#include "i2cdrv.h"

#define SGP30_ADDR	(0x58<<1)

class JsonNumber;


struct SGP30 : public I2CDevice
{
	explicit SGP30(uint8_t port)
	: I2CDevice(port,SGP30_ADDR,drvName())
	{ }

	SGP30(uint8_t port, uint8_t addr, const char *n)
	: I2CDevice(port,addr,n)
	{ }


	static SGP30 *create(uint8_t bus);

	const char *drvName() const;
	int init();
	uint8_t status();
	void attach(class JsonObject *);

	protected:
	float calc_press(int32_t adc_P, int32_t t_fine);
	int32_t calc_tfine(int32_t adc_T);
	void updateHumidity();
	static unsigned cyclic(void *arg);
	int sample();
	int read();
	int selftest_start();
	int selftest_finish();

	JsonNumber *m_tvoc = 0, *m_co2 = 0;	// owned
	JsonNumber *m_temp = 0, *m_humid = 0;	// refered to
	JsonObject *m_root = 0;
	uint16_t m_ahumid = 0;
	typedef enum { selftest, idle, update_humid, measure, error } state_t;
	state_t m_state = selftest;
};


unsigned sgp30_scan(uint8_t);


#endif
