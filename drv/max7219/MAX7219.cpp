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

#include "MAX7219.h"

#include "log.h"

#include <string.h>

#define MAXFREQDEL 1

static char TAG[] = "MAX7219";


int MAX7219Drv::init(gpio_num_t clk, gpio_num_t dout, gpio_num_t cs, bool odrain)
{
	if (m_attached) {
		log_error(TAG,"cannot attach: device already attached");
		return 1;
	}
	memset(m_digits,0xff,8);
	m_clk = clk;
	m_dout = dout;
	m_cs = cs;
	m_odrain = odrain;
	m_attached = true;
	m_intensity = 0;
	if (ESP_OK != gpio_set_direction(m_clk, m_odrain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of clock gpio %d",m_clk);
	} else if (ESP_OK != gpio_set_direction(m_dout, m_odrain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of dout gpio %d",m_dout);
	} else if (ESP_OK != gpio_set_direction(m_cs, GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of cs gpio %d",m_cs);
	} else {
		log_info(TAG,"attached driver");
		chip_select(1);
		clock(0);
		return 0;
	}
	return 1;
}


void MAX7219Drv::clock(int c)
{
	if (ESP_OK != gpio_set_level(m_clk,c)) {
		log_error(TAG,"cannot set level of clk at gpio %d",m_cs);
	}
}


void MAX7219Drv::dout(int c)
{
	if (ESP_OK != gpio_set_level(m_dout,c!=0))
		log_error(TAG,"cannot set level of dout at gpio %d",m_cs);
}


void MAX7219Drv::chip_select(int c)
{
	if (ESP_OK != gpio_set_level(m_cs,c))
		log_error(TAG,"cannot set level of cs at gpio %d",m_cs);
}

void MAX7219Drv::setreg(uint8_t r, uint8_t v)
{
	//log_info(TAG,"setreg(0x%x,0x%x)",r,v);
	gpio_set_level(m_clk,0);
	gpio_set_level(m_cs,0);
	uint8_t b = 8;
	do {
		gpio_set_level(m_clk,0);
		gpio_set_level(m_dout,(r&0x80) != 0);
		r <<= 1;
		gpio_set_level(m_clk,1);
	} while (--b);
	b = 8;
	do {
		gpio_set_level(m_clk,0);
		gpio_set_level(m_dout,(v&0x80) != 0);
		v <<= 1;
		gpio_set_level(m_clk,1);
	} while (--b);
	gpio_set_level(m_cs,1);
	gpio_set_level(m_clk,0);
}


void MAX7219Drv::setDigit(uint8_t d, uint8_t v)
{
	assert(d < sizeof(m_digits)/sizeof(m_digits[0]));
	if (v == m_digits[d])
		return;
//	log_dbug(TAG,"setDigit(%d,0x%x)",d,v);
	m_digits[d] = v;
	setreg(d+1,v);
}


void MAX7219Drv::powerup()
{
	setreg(0x0c,1);
}


void MAX7219Drv::shutdown()
{
	setreg(0x0c,0);
}


void MAX7219Drv::displayTest(bool t)
{
	setreg(0x0f,t ? 1 : 0);
}


void MAX7219Drv::setDigits(uint8_t n)	// set number of scanned digits
{
	setreg(0xb,n-1);
}


void MAX7219Drv::setIntensity(uint8_t i)
{
	setreg(0xa,i);
	m_intensity = i;
}


void MAX7219Drv::setDecoding(uint8_t modes)	// bit 0..7 => digit 0..7; 1 for code b
{
	setreg(0x9,modes);
}
