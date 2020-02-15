/*
 *  Copyright (C) 2018, Thomas Maier-Komor
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

#ifndef TLC5947_H
#define TLC5947_H

#include <stdint.h>

#include <driver/gpio.h>


class TLC5947
{
	public:
	TLC5947(gpio_num_t sin, gpio_num_t sclk, gpio_num_t xlat, gpio_num_t blank, unsigned num = 1);

	void init();
	void set_led(unsigned x, uint16_t v);
	uint16_t get_led(unsigned x);
	void commit();
	void on();
	void off();

	private:
	gpio_num_t m_sin, m_sclk, m_xlat, m_blank;
	uint16_t m_nled;
	uint16_t *m_data;
};

#endif
