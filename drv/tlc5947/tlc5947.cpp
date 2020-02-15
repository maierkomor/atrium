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

#include "tlc5947.h"
#include "log.h"

#include <stdlib.h>

static char TAG[] = "TLC5946";

#define OE_high		gpio_set_level(m_oe,1)
#define OE_low		gpio_set_level(m_oe,0)
#define LE_high		gpio_set_level(m_le,1)
#define LE_low		gpio_set_level(m_le,0)
#define CLK_high	gpio_set_level(m_clk,1)
#define CLK_low		gpio_set_level(m_clk,0)
#define SDI_high	gpio_set_level(m_sdi,1)
#define SDI_low		gpio_set_level(m_sdi,0)
#define SDO		gpio_get_level(m_sdo)


TLC5947::TLC5947(gpio_num_t sin, gpio_num_t sclk, gpio_num_t xlat, gpio_num_t blank, unsigned num)
: m_sin(sin)
, m_sclk(sclk)
, m_xlat(xlat)
, m_blank(blank)
, m_nled(num*24)
, m_data(0)
{
}


void TLC5947::init()
{
	log_info(TAG,"initializing for %u leds",m_nled);
	m_data = (uint16_t *) malloc(m_nled*sizeof(uint16_t));
	if (esp_err_t e = gpio_set_direction(m_sclk,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for SCLK to output: 0x%x",e);
	}
	if (esp_err_t e = gpio_set_direction(m_sin,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for SIN to output: 0x%x",e);
	}
	if (esp_err_t e = gpio_set_direction(m_blank,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for BLANK to output: 0x%x",e);
	}
	if (esp_err_t e = gpio_set_direction(m_xlat,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for XLAT to output: 0x%x",e);
	}
	gpio_set_level(m_xlat,0);
	gpio_set_level(m_blank,1);
}


void TLC5947::commit()
{
	size_t nled = m_nled;
	uint16_t *l = m_data;
	do {
		uint16_t d = *l++;
		for (int b = 0; b < 12; ++b) {
			gpio_set_level(m_sclk,1);
			gpio_set_level(m_sin,(d & (1<<11)) ? 1 : 0);
			d <<= 1;
			gpio_set_level(m_sclk,0);
		}
	} while (--nled);
	gpio_set_level(m_xlat,1);
	gpio_set_level(m_xlat,0);
	log_info(TAG,"commit()");
}


void TLC5947::on()
{
	if (esp_err_t e = gpio_set_level(m_blank,0)) {
		log_error(TAG,"unable to set blank to low: 0x%x",e);
	}
}


void TLC5947::off()
{
	if (esp_err_t e = gpio_set_level(m_blank,1)) {
		log_error(TAG,"unable to set blank to high: 0x%x",e);
	}
}


void TLC5947::set_led(unsigned x, uint16_t v)
{
	if (x >= m_nled) {
		log_error(TAG,"set_led(): led %u out of range",x);
		return;
	}
	log_info(TAG,"set_led(%u,%u)",x,v);
	m_data[x] = v;
}


uint16_t TLC5947::get_led(unsigned x)
{
	if (x >= m_nled) {
		log_error(TAG,"get_led(): led %u out of range",x);
		return 0;
	}
	return m_data[x];
}
