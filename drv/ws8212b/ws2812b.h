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

#ifndef WS2812B_DRV_H
#define WS2812B_DRV_H


#include <driver/gpio.h>

#ifdef ESP32
#include <driver/rmt.h>
#elif defined ESP8266
typedef uint8_t rmt_channel_t;
typedef struct rmt_item32_s rmt_item32_t;
#endif

class WS2812BDrv
{
	public:
	WS2812BDrv(rmt_channel_t ch, gpio_num_t gpio, size_t nleds);
	~WS2812BDrv();

	bool init();
	void set_led(size_t l, uint8_t r, uint8_t g, uint8_t b);
	void set_led(size_t l, uint32_t rgb);
	void set_leds(uint32_t rgb);

	void update();
	void reset();

	private:
	WS2812BDrv(const WS2812BDrv &);
	WS2812BDrv& operator = (const WS2812BDrv &);

	uint8_t *m_values;
	rmt_item32_t *m_items;
	size_t m_num;
	gpio_num_t m_gpio;
	rmt_channel_t m_ch;	// ignored on ESP8266
	bool m_ready;
};

#endif
