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

#ifndef WS2812B_DRV_H
#define WS2812B_DRV_H

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include <driver/gpio.h>

#ifdef CONFIG_IDF_TARGET_ESP32
#include <driver/rmt.h>
#else
typedef uint8_t rmt_channel_t;
typedef struct rmt_item32_s rmt_item32_t;
#endif

class WS2812BDrv
{
	public:
	WS2812BDrv()
	: m_set(0)
	, m_cur(0)
	, m_items(0)
	, m_num(0)
	, m_tmr(0)
	{ }

	int init(gpio_num_t gpio, size_t nleds, rmt_channel_t ch = (rmt_channel_t)0);
	void set_led(size_t l, uint8_t r, uint8_t g, uint8_t b);
	void set_led(size_t l, uint32_t rgb);
	void set_leds(uint32_t rgb);

	void update(bool fade = false);
	void reset();

	private:
	WS2812BDrv(const WS2812BDrv &);
	WS2812BDrv& operator = (const WS2812BDrv &);

	static void timerCallback(void *);
	void commit();

	uint8_t *m_set, *m_cur;
	rmt_item32_t *m_items;
	size_t m_num;
	TimerHandle_t m_tmr;
	gpio_num_t m_gpio;
#ifdef CONFIG_IDF_TARGET_ESP32
	rmt_channel_t m_ch;	// ignored on CONFIG_IDF_TARGET_ESP8266
#elif defined CONFIG_IDF_TARGET_ESP8266
	uint8_t m_t0l,m_t0h,m_t1l,m_t1h;
	uint16_t m_tr;
#endif
};

#endif
