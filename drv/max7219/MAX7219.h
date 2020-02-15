/*
 *  Copyright (C) 2018-2019, Thomas Maier-Komor
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

//#include <driver/spi.h> -- for ESP8266???
#include <driver/gpio.h>

//
// 5V level adjustment necessary 
//

class MAX7219Drv
{
	public:
	MAX7219Drv(gpio_num_t clk, gpio_num_t dout, gpio_num_t cs, bool odrain)
	: m_clk(clk)
	, m_dout(dout)
	, m_cs(cs)
	, m_odrain(odrain)
	, m_attached(false)
	{ }

	~MAX7219Drv();
	
	void attach();
	void detach();
	void powerup();
	void shutdown();
	void setDigits(uint8_t);
	void setIntensity(uint8_t);
	void setDecoding(uint8_t);
	void displayTest(bool);
	void setDigit(uint8_t d, uint8_t v);

	private:
	void setreg(uint8_t r, uint8_t v);
	void clock(int);
	void dout(int);
	void chip_select(int);

	gpio_num_t m_clk, m_dout, m_cs;
	bool m_odrain, m_attached;
};


#endif
