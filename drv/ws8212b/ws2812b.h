/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#if IDF_VERSION >= 44
#include <soc/soc_caps.h>
#if defined SOC_RMT_GROUPS && SOC_RMT_GROUPS > 0
#if IDF_VERSION >= 50
#include <driver/rmt_tx.h>
#else
#include <driver/rmt.h>
#endif
#endif
#endif

#ifndef SOC_RMT_GROUPS
#define SOC_RMT_GROUPS 0
#endif

#if SOC_RMT_GROUPS == 0
typedef uint8_t rmt_channel_t;
typedef struct rmt_item32_s rmt_item32_t;
#endif

class WS2812BDrv
{
	public:
	WS2812BDrv(const char *n)
	: m_name(strdup(n))
	, m_set(0)
	, m_cur(0)
	, m_num(0)
	, m_tmr(0)
	{
		// The first bus must stay the first bus, so that
		// all functions without bus argument refer to the same
		// bus, even if adding a bus later.
		if (First) {
			m_next = First->m_next;
			First->m_next = this;
		} else {
			First = this;
		}
	}

	~WS2812BDrv()
	{
		free(m_name);
	}

	int init(gpio_num_t gpio, size_t nleds, int ch = -1);
	void set_led(size_t l, uint8_t r, uint8_t g, uint8_t b);
	void set_led(size_t l, uint32_t rgb);
	void set_leds(uint32_t rgb);
	uint32_t get_led(unsigned led);
	void update(bool fade = false);
	void reset();

	size_t num_leds() const
	{ return m_num; }

	static WS2812BDrv *get_bus(const char *n);
	static WS2812BDrv *first()
	{ return First; }

	private:
	WS2812BDrv(const WS2812BDrv &);
	WS2812BDrv& operator = (const WS2812BDrv &);

	static void timerCallback(TimerHandle_t h);
	void commit();

	static WS2812BDrv *First;
	char *m_name;
	WS2812BDrv *m_next = 0;
	uint8_t *m_set, *m_cur;
#if defined SOC_RMT_GROUPS && SOC_RMT_GROUPS > 0
#if IDF_VERSION >= 50
	rmt_encoder_t *m_benc = 0, *m_cenc = 0;
	rmt_symbol_word_t m_rstsym;
	rmt_channel_handle_t m_ch;
#else
	rmt_item32_t *m_items = 0;
#endif
#endif
	size_t m_num;
	TimerHandle_t m_tmr;
	gpio_num_t m_gpio;
#if defined SOC_RMT_GROUPS && SOC_RMT_GROUPS > 0
#if IDF_VERSION < 50
	rmt_channel_t m_ch;
#endif
#else
	uint8_t m_t0l,m_t0h,m_t1l,m_t1h;
	uint16_t m_tr;
#endif
};

#endif
