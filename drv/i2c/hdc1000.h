/*
 *  Copyright (C) 2018-2021, Thomas Maier-Komor
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

#include "i2cdrv.h"

#define HDC1000_ADDR (0x40<<1)

class JsonNumber;

struct HDC1000 : public I2CDevice
{
	const char *drvName() const
	{ return "hdc1000"; }

	int init();
	void attach(class JsonObject *);
	unsigned cyclic();

	static HDC1000 *create(uint8_t bus);

	protected:
	explicit HDC1000(uint8_t port)
	: I2CDevice(port,HDC1000_ADDR,drvName())
	{ }

	static void trigger(void *);
	static void trigger_humid(void *);
	static void trigger_temp(void *);
	bool status();
	bool sample();
	bool read();
	void handle_error();
	int setSingle(bool);
	void setTemp(uint8_t data[]);
	void setHumid(uint8_t data[]);

	JsonNumber *m_temp = 0, *m_humid = 0;
	typedef enum { st_idle, st_readtemp, st_readhumid, st_readboth } state_t;
	typedef enum { sm_none, sm_temp, sm_humid, sm_seq, sm_both } sample_t;
	state_t m_state = st_idle;
	sample_t m_sample = sm_none;
	bool m_single;
};


#endif
