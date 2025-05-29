/*
 *  Copyright (C) 2021-2025, Thomas Maier-Komor
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

#ifndef DS18B20_H
#define DS18B20_H

#include "env.h"
#include "owdevice.h"


struct DS18B20 : public OwDevice
{
	public:
	static int create(uint64_t id, const char *name = 0);

	const char *deviceType() const override
	{ return "DS18B20"; }

	void attach(EnvObject *);

	private:
	typedef enum res_e { res_9b, res_10b, res_11b, res_12b } res_t;

	explicit DS18B20(uint64_t id, const char *name);

	void read();
	void set_resolution(res_t);
	static unsigned cyclic(void *);
	static void sample(void *);
	static void set_res9b(void *);
	static void set_res10b(void *);
	static void set_res11b(void *);
	static void set_res12b(void *);

	class EnvNumber m_env;
	typedef enum state_e { st_idle = 0, st_sample, st_read, st_set9b, st_set10b, st_set11b, st_set12b } state_t;
	state_t m_st = st_idle;
	res_t m_res = res_12b;
	uint8_t m_err = 0;
};


#endif
