/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#ifndef HLW8012_H
#define HLW8012_H

#include "event.h"
#include <driver/gpio.h>

class EnvObject;

class HLW8012
{
	public:
	// Either cf or cf1 must be given. cf measures power, and cf1
	// can be swichted via sel to measure voltage or current
	// If sel is not provided, it is assumed to be stuck at GND - i.e.
	// measure current.
	static HLW8012 *create(int8_t sel, int8_t cf, int8_t cf1);

	void attach(EnvObject *);

	private:
	HLW8012(int8_t sel, int8_t cf, int8_t cf1);

	static void calcCurrent(void *arg);
	static void calcPower(void *arg);
	static void calcVoltage(void *arg);
	static void intrHandlerCF(void *arg);
	static void intrHandlerCF1(void *arg);

	uint64_t m_tscf[3];
	uint64_t m_tscf1[3];
	char m_name[12];
	class EnvNumber *m_curr = 0, *m_volt = 0, *m_power = 0;
	gpio_num_t m_sel, m_cf, m_cf1;
	event_t m_ep = 0, m_ev = 0, m_ec = 0;
	typedef enum state_e { wait_rise, wait_fall } state_t;
	state_t m_st;
};


#endif
