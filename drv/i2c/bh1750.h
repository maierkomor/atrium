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

#ifndef BH1750_H
#define BH1750_H

#include "i2cdrv.h"


class BH1750 : public I2CDevice
{
	public:
	static BH1750 *create(uint8_t bus, uint8_t addr);

	const char *drvName() const override
	{ return "bh1750"; }

	void attach(class EnvObject *) override;

	private:
	typedef enum { st_undef, st_off, st_idle, st_sampling, st_err } state_t;
	typedef enum { rq_none, rq_off, rq_on, rq_sample, rq_qsample, rq_reset } req_t;

	BH1750(uint8_t b, uint8_t a)
	: I2CDevice(b,a,"bh1750")
	, m_st(st_idle)
	{ }

	static unsigned cyclic(void*);
	static void sample(void*);
	static void qsample(void*);
	static void on(void*);
	static void off(void*);
	static void reset(void*);

	class EnvNumber *m_lux = 0;
	state_t m_st = st_undef;
	req_t m_rq = rq_none;
};


#endif
