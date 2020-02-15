/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

#ifndef HC_SR04_H
#define HC_SR04_H

#ifdef __cplusplus
#include <stdint.h>
#include <driver/gpio.h>

class HC_SR04
{
	public:
	HC_SR04();

	int init(unsigned trigger, unsigned echo); 
	int measure(unsigned *);

	private:
	static void hc_sr04_isr(void *);

	int64_t m_start;
	unsigned m_delta;
	gpio_num_t m_trigger, m_echo;
};

inline HC_SR04::HC_SR04()
: m_trigger(GPIO_NUM_MAX)
, m_echo(GPIO_NUM_MAX)
{

}

extern "C"
#endif
int hc_sr04(int argc, char *args[]);

#endif
