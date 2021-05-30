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

#ifndef DRV_MAX7219_H
#define DRV_MAX7219_H

#include "ledcluster.h"

//#include <driver/spi.h> -- for ESP8266???
#include <driver/gpio.h>

//
// 5V level adjustment necessary 
//

class MAX7219Drv : public LedCluster
{
	public:
	static MAX7219Drv *create(gpio_num_t clk, gpio_num_t dout, gpio_num_t cs, bool odrain);

	void clear();
	uint8_t getDim() const
	{ return m_intensity; }
	uint8_t maxDim() const
	{ return UINT8_MAX; }
	void setDigits(uint8_t);
	int setDim(uint8_t);
	int setOffset(unsigned);
	int setOn(bool);
	int setNumDigits(unsigned);
	int setPos(unsigned x, unsigned y);
	int write(uint8_t v, int off = -1);
//	int write(uint8_t *d, unsigned n, int off = -1);
	void setDecoding(uint8_t);
	void displayTest(bool);

	private:
	MAX7219Drv(gpio_num_t clk, gpio_num_t dout, gpio_num_t cs);

	void setreg(uint8_t r, uint8_t v);
	void clock(int);
	void dout(int);
	void chip_select(int);

	gpio_num_t m_clk, m_dout, m_cs;
	uint8_t m_digits[8];
	uint8_t m_at = 0, m_intensity = 0, m_ndigits = 8;
};


#endif
