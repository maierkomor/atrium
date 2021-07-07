/*
 *  Copyright (C) 2018-2021, Thomas Maier-Komor
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

#include <sdkconfig.h>

#if defined CONFIG_MAX7219

#include "MAX7219.h"
#include "log.h"

#include <string.h>

static char TAG[] = "MAX7219";


MAX7219Drv::MAX7219Drv(gpio_num_t clk, gpio_num_t dout, gpio_num_t cs)
: LedCluster()
, m_clk(clk)
, m_dout(dout)
, m_cs(cs)
{
	bzero(m_digits,sizeof(m_digits));
	chip_select(1);
	clock(0);
	displayTest(true);
	displayTest(false);
	clear();
	setDecoding(0x0);
	setDim(15);
	setOn(true);
}


MAX7219Drv *MAX7219Drv::create(gpio_num_t clk, gpio_num_t dout, gpio_num_t cs, bool odrain)
{

	if (ESP_OK != gpio_set_direction(clk, odrain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of clock gpio %d",clk);
		return 0;
	}
	if (ESP_OK != gpio_set_direction(dout, odrain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of dout gpio %d",dout);
		return 0;
	}
	if (ESP_OK != gpio_set_direction(cs, GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set direction of cs gpio %d",cs);
		return 0;
	}
	log_dbug(TAG,"attached driver");
	return new MAX7219Drv(clk,dout,cs);
}


void MAX7219Drv::clock(int c)
{
	if (ESP_OK != gpio_set_level(m_clk,c))
		log_error(TAG,"cannot set level of clk at gpio %d",m_cs);
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


int MAX7219Drv::setPos(uint8_t x, uint8_t y)
{
	log_dbug(TAG,"setPos(%u,%u)",(unsigned)x,(unsigned)y);
	if (y > 0)
		return -1;
	if (x >= sizeof(m_digits))
		return -1;
	m_at = x;
	return 0;
}


int MAX7219Drv::write(uint8_t v)
{
	log_dbug(TAG,"write(0x%x), at %u",v,m_at);
	if (v != m_digits[m_at]) {
		m_digits[m_at] = v;
		setreg(8-m_at,v);
	}
	++m_at;
	if (m_at == sizeof(m_digits))
		m_at = 0;
	return 0;
}


int MAX7219Drv::clear()
{
	int i = 0;
	do {
		m_digits[i] = 0;
		++i;
		setreg(i,0x00);
	} while (i < 8);
	return 0;
}


int MAX7219Drv::setOn(bool on)
{
	setreg(0x0c,on);
	return 0;
}


void MAX7219Drv::displayTest(bool t)
{
	setreg(0x0f,t ? 1 : 0);
}


int MAX7219Drv::setNumDigits(unsigned n)	// set number of scanned digits
{
	if (n > 8) {
		setreg(0xb,7);
		return -1;
	}
	setreg(0xb,n-1);
	return 0;
}


int MAX7219Drv::setDim(uint8_t i)
{
	if (i != m_intensity) {
		setreg(0xa,i);
		m_intensity = i;
	}
	return 0;
}


void MAX7219Drv::setDecoding(uint8_t modes)	// bit 0..7 => digit 0..7; 1 for code b
{
	setreg(0x9,modes);
}


#endif
