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

#include "MAX7219.h"

#include "log.h"

#define MAXFREQDEL 1

static char TAG[] = "MAX7219";
static volatile int V;


MAX7219Drv::~MAX7219Drv()
{
	if (m_attached)
		detach();
}


void MAX7219Drv::attach()
{
	if (m_attached) {
		log_error(TAG,"cannot attach: device already attached");
		return;
	}
	if (ESP_OK != gpio_set_direction(m_clk, m_odrain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of clock gpio %d",m_clk);
	} else if (ESP_OK != gpio_set_direction(m_dout, m_odrain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of dout gpio %d",m_dout);
	} else if (ESP_OK != gpio_set_direction(m_cs, GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of cs gpio %d",m_cs);
	} else {
		log_info(TAG,"attached driver");
		return;
	}
	chip_select(1);
	clock(0);
}


void MAX7219Drv::detach()
{
	if (!m_attached) {
		log_error(TAG,"cannot detach: device not attached");
		return;
	}
	if (ESP_OK != gpio_set_direction(m_clk, GPIO_MODE_DISABLE)) {
		log_error(TAG,"cannot disable clock gpio %d",m_clk);
	}
	if (ESP_OK != gpio_set_direction(m_dout, GPIO_MODE_DISABLE)) {
		log_error(TAG,"cannot disable dout gpio %d",m_dout);
	}
	if (ESP_OK != gpio_set_direction(m_cs, GPIO_MODE_DISABLE)) {
		log_error(TAG,"cannot disable cs gpio %d",m_cs);
	}
	log_info(TAG,"detached driver");
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
//	log_dbug(TAG,"setDigit(%d,0x%x)",d,v);
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
}


void MAX7219Drv::setDecoding(uint8_t modes)	// bit 0..7 => digit 0..7; 1 for code b
{
	setreg(0x9,modes);
}
