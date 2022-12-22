/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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
	TLC5947()
	: m_initialized(false)
	, m_on(false)
	{ }

	int init(gpio_num_t sin, gpio_num_t sclk, gpio_num_t xlat, gpio_num_t blank, unsigned num = 1);
	void set_led(unsigned x, uint16_t v);
	uint16_t get_led(unsigned x);
	void commit();
	void on();
	void off();
	bool is_on() const
	{ return m_on; }
	esp_err_t get_error() const
	{ return m_err; }
	bool initialized() const
	{ return m_initialized; }
	unsigned get_nleds() const
	{ return m_nled; }

	private:
	gpio_num_t m_sin, m_sclk, m_xlat, m_blank;
	esp_err_t m_err;
	uint16_t m_nled;
	uint16_t *m_data;
	bool m_initialized, m_on;
};

#endif
