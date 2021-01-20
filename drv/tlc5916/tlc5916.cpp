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

#include "tlc5916.h"
#include "log.h"

#include <rom/gpio.h>

static char TAG[] = "TLC5916";

#define OE_high		gpio_set_level(m_oe,1)
#define OE_low		gpio_set_level(m_oe,0)
#define LE_high		gpio_set_level(m_le,1)
#define LE_low		gpio_set_level(m_le,0)
#define CLK_high	gpio_set_level(m_clk,1)
#define CLK_low		gpio_set_level(m_clk,0)
#define SDI_high	gpio_set_level(m_sdi,1)
#define SDI_low		gpio_set_level(m_sdi,0)
#define SDO		gpio_get_level(m_sdo)



int TLC5916::init(gpio_num_t clk, gpio_num_t sdi, gpio_num_t le, gpio_num_t oe, gpio_num_t sdo)
{
	if (m_initialized) {
		log_error(TAG,"already initialized");
		return 1;
	}
	m_clk = clk;
	m_sdi = sdi;
	m_le = le;
	m_oe = oe;
	m_sdo = sdo;
	gpio_pad_select_gpio(clk);
	gpio_pad_select_gpio(sdi);
	gpio_pad_select_gpio(le);
	gpio_pad_select_gpio(oe);
	gpio_pad_select_gpio(sdo);
	if (esp_err_t e = gpio_set_direction(m_clk,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for CLK to output: 0x%x",e);
	} else if (esp_err_t e = gpio_set_direction(m_sdi,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for SDI to output: 0x%x",e);
	} else if (esp_err_t e = gpio_set_direction(m_le,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for LE to output: 0x%x",e);
	} else if (esp_err_t e = gpio_set_direction(m_oe,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for OE to output: 0x%x",e);
	} else if (esp_err_t e = gpio_set_direction(m_sdo,GPIO_MODE_INPUT)) {
		log_error(TAG,"unable to gpio for SDO to input: 0x%x",e);
	} else {
		m_initialized = true;
		config();
		return 0;
	}
	return 1;
}


void TLC5916::set_vgain(uint8_t g)
{
	if (g & 0x80) {
		log_error(TAG,"invalid argument: gain must be 0..127");
		return;
	}
	m_config = (m_config & 0x80) | g;
	config();
}


void TLC5916::set_config(bool cm, bool hc, uint8_t cc)
{
	if (cc & 0xc0) {
		log_error(TAG,"invalid argument: current gain must be 0..63");
		return;
	}
	m_config = cm ? 0x80 : 0;
	if (hc)
		m_config |= 0x40;
	m_config |= cc;
	config();
}


void TLC5916::config()
{
	special();
	uint8_t config = m_config;
	OE_high;
	LE_low;
	for (unsigned b = 0; b < 7; ++b) {
		CLK_low;
		if (config & 0x1)
			SDI_high;
		else
			SDI_low;
		config >>= 1;
		CLK_high;
	}
	CLK_low;
	LE_high;
	if (config & 0x1)
		SDI_high;
	else
		SDI_low;
	CLK_high;
	CLK_low;
	LE_low;
	normal();
}


void TLC5916::normal()
{
	CLK_low;
	OE_high;
	LE_low;
	CLK_high;	// oe=1,le=0
	OE_low;
	CLK_low;
	CLK_high;	// oe=0,le=0
	OE_high;
	CLK_low;
	CLK_high;	// oe=1,le=0
	CLK_low;
	CLK_high;	// oe=1,le=0
	CLK_low;
	CLK_high;	// oe=1,le=0
	CLK_low;
}


void TLC5916::special()
{
	CLK_low;
	OE_high;
	LE_low;
	CLK_high;	// le=0,oe=1
	CLK_low;
	OE_low;
	CLK_high;	// le=0,oe=0
	OE_high;
	CLK_low;	// le=0,oe=1
	CLK_high;
	LE_high;
	CLK_low;	// le=1,oe=1
	//CLK_LE_toggle;
	CLK_high;
	LE_low;
	CLK_low;	// le=0,oe=1
	CLK_high;
	CLK_low;	// le=0,oe=1
}


void TLC5916::set1(uint8_t v)
{
	uint8_t b;
	//OE_high;
	for (b = 0; b < 8; ++b) {
		CLK_low;
		if (v & 0x80U)
			SDI_high;
		else
			SDI_low;
		CLK_high;	// shifted here
		v <<= 1;
	}
	CLK_low;
	LE_high;
	LE_low;
	OE_low;
}


uint8_t TLC5916::get_error()
{
	uint8_t err = 0;
	CLK_low;
	OE_high;
	CLK_high;
	CLK_low;
	OE_low;
	CLK_high;
	CLK_low;
	CLK_high;
	CLK_low;
	CLK_high;
	CLK_low;
	CLK_high;
	OE_high;
	int b;
	for (b = 0; b < 8; ++b) {
		err <<= 1;
		CLK_low;
		err |= SDO;
		CLK_high;
	}
	return err;
}


#ifdef RUNTEST

int main()
{
	int x;
	uint8_t d;
	TLC5916::init();

	OE_low;
	LE_low;
	CLK_low;
	// config bit 7:
	// 	cm=0: 3-40mA
	//	cm=1: 10-120mA
	// config bit 6: HC
	// config bit 0-5: D
	// VG = (1 + HC) × (1 + D/64) / 4
	//TLC5916::special();
	//TLC5916::config(0x3f);
	//TLC5916::config(0x0);
	//TLC5916::normal();

	// default VG=127/128
	// at R_Ref=720 Ohm => 26mA
	//TLC5916::normal();
	DDRB |= (1<<PB5);	// mini-pro onboard led

	TLC5916::set1(0xff);
	while (1) {
		x = 0x3f;
		while (x > 0) {
			TLC5916::special();
			TLC5916::config(x);
			TLC5916::normal();
			//TLC5916::set1(x|0xc0);
			OE_low;
			_delay_ms(70);
			x--;
		}
	}
	/*
	for (;;) {
		for (d=0; d<3; ++d) {
			_delay_ms(500);
			PORTB ^= (1<<PB5);
			TLC5916::set1(0xff);
			_delay_ms(500);
			PORTB ^= (1<<PB5);
			TLC5916::set1(0x0);
		}
		for (d=0; d<3; ++d) {
			_delay_ms(500);
			PORTB ^= (1<<PB5);
			TLC5916::set1(0xff);
			_delay_ms(500);
			PORTB ^= (1<<PB5);
			TLC5916::set1(0x0);
		}
		d = 0x80;
		do {
			_delay_ms(500);
			PORTB ^= (1<<PB5);
			TLC5916::set1(d);
			d>>=1;
		} while(d);
		d = 0xff;
		do {
			_delay_ms(500);
			PORTB ^= (1<<PB5);
			TLC5916::set1(d); d>>=1;
		} while(d);
	}
	*/
}
#endif
