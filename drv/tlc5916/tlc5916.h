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

#ifndef TLC5916_H
#define TLC5916_H

#include <stdint.h>

#include <driver/gpio.h>


class TLC5916
{
	public:
	TLC5916()
	: m_initialized(false)
	{ }

	int init(gpio_num_t clk, gpio_num_t sdi, gpio_num_t le, gpio_num_t oe, gpio_num_t sdo);
	void set_config(bool cm, bool hc, uint8_t cc);
	void set1(uint8_t);
	void set_all(uint8_t);
	void set_mult(const uint8_t *, uint8_t num);
	void set_vgain(uint8_t);
	void set_cmult(bool);
	uint8_t get_error();

	protected:
	void normal();
	void special();

	private:
	void config();

	gpio_num_t m_clk,m_sdi,m_le,m_oe,m_sdo;
	uint8_t m_config;	// in reverse bit order
	bool m_initialized;
};


#endif
