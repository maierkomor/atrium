/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#ifdef CONFIG_TLC5947
#include "tlc5947.h"
#include "log.h"

#include <stdlib.h>
#include <strings.h>

#if defined CONFIG_IDF_TARGET_ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif


#define TAG MODULE_TLC5947


int TLC5947::init(gpio_num_t sin, gpio_num_t sclk, gpio_num_t xlat, gpio_num_t blank, unsigned num)
{
	if (m_initialized) {
		log_error(TAG,"already initialized");
		return 1;
	}
	m_sin = sin;
	m_sclk = sclk;
	m_xlat = xlat;
	m_blank = blank;
	m_nled = num * 24;

	log_info(TAG,"initializing for %u leds",m_nled);
	gpio_pad_select_gpio(sin);
	gpio_pad_select_gpio(sclk);
	gpio_pad_select_gpio(xlat);
	gpio_pad_select_gpio(blank);
	m_data = (uint16_t *) malloc(m_nled*sizeof(uint16_t));
	if (esp_err_t e = gpio_set_direction(m_sclk,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for SCLK to output: 0x%x",e);
	} else if (esp_err_t e = gpio_set_direction(m_sin,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for SIN to output: 0x%x",e);
	} else if (esp_err_t e = gpio_set_direction(m_blank,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for BLANK to output: 0x%x",e);
	} else if (esp_err_t e = gpio_set_direction(m_xlat,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"unable to gpio for XLAT to output: 0x%x",e);
	} else {
		gpio_set_level(m_xlat,0);
		gpio_set_level(m_blank,0);
		for (int i = 0; i < m_nled; ++i)
			m_data[i] = (1<<12)-1;
//		commit();
		return 0;
	}
	return 1;
}


void TLC5947::commit()
{
	size_t nled = m_nled;
	uint16_t *l = m_data;
//	char buf[256], *b = buf;
//	for (int i = 0; i < nled; ++i)
//		b += snprintf(b,sizeof(buf)-(b-buf),"%u ",(unsigned)m_data[i]);
//	log_dbug(TAG,"%s",buf);
	do {
		uint16_t d = *l++;
//		log_dbug(TAG,"%u",(unsigned)d);
		for (int b = 0; b < 12; ++b) {
			gpio_set_level(m_sclk,0);
			gpio_set_level(m_sin,(d & (1<<12)) ? 1 : 0);
			d <<= 1;
			gpio_set_level(m_sclk,1);
		}
	} while (--nled);
	gpio_set_level(m_xlat,1);
	gpio_set_level(m_xlat,0);
//	log_info(TAG,"commit()");
}


void TLC5947::on()
{
	if (esp_err_t e = gpio_set_level(m_blank,0))
		log_warn(TAG,"unable to set blank to low: %s",esp_err_to_name(e));
	else
		m_on = true;
}


void TLC5947::off()
{
	if (esp_err_t e = gpio_set_level(m_blank,1))
		log_warn(TAG,"unable to set blank to high: %s",esp_err_to_name(e));
	else
		m_on = false;
}


void TLC5947::set_led(unsigned x, uint16_t v)
{
	if (x >= m_nled) {
		log_error(TAG,"set_led(): led %u out of range",x);
		return;
	}
	if (v >= (1<<12)) {
		log_error(TAG,"set_led(): value %u out of range",v);
		return;
	}
//	log_info(TAG,"set_led(%u,%u)",x,v);
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


#endif // CONFIG_TLC5947
