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

#include <sdkconfig.h>

#ifdef CONFIG_LEDSTRIP
// ESP32 rmt does not work before IDF release v3.2!!!
// ESP8266 for some reason doesn't achieve the necessary performance!!!
// Placing the critical section in IRAM didn't give any benfit.

#include "log.h"
#include "ws2812b.h"

#include <stdlib.h>
#include <string.h>
#include <driver/gpio.h>
#include <rom/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_attr.h>
#include <esp_err.h>
#include <esp_timer.h>

static char TAG[] = "ws2812";

// ESP8266: TODO: timing is UNTESTED!

#if defined CONFIG_ESP32_DEFAULT_CPU_FREQ_80
// ESP32 at 80MHz, numbers in cycles
#define T0H	28	// 0.35us
#define T0L	64	// 0.80us
#define T1H	56	// 0,70us
#define T1L	48	// 0.60us
#define TR	4400	// >50us=4000
#elif defined CONFIG_ESP32_DEFAULT_CPU_FREQ_160
// ESP32 at 160MHz, numbers in cycles -- does not work! cycles incorrect?
#define T0H	60	// 0.35us
#define T0L	130	// 0.80us
#define T1H	128	// 0,70us
#define T1L	70	// 0.60us
#define TR	8000	// >50us=4000
#elif defined CONFIG_ESP8266_DEFAULT_CPU_FREQ_80
// TODO: fix ESP8266
// ESP8266 at 80MHz, numbers in iterations
#define T0H	32	// 0.4us
#define T0L	68	// 0.85us
#define T1H	64	// 0,80us
#define T1L	36	// 0.45us
#define TR	4400	// >50us=4000
#elif defined CONFIG_ESP8266_DEFAULT_CPU_FREQ_160 
// ESP8266 at 160MHz, numbers in iterations
#define T0H	56	// 0.35us
#define T0L	128	// 0.80us
#define T1H	112	// 0,70us
#define T1L	96	// 0.60us
#define TR	8800	// >50us=4000
#else
#error unexpected default cpu frequency
#endif


#ifdef ESP8266

typedef struct rmt_item32_s {
	uint16_t duration0;
	uint16_t duration1;
} rmt_item32_t;


static IRAM_ATTR uint32_t write_items(gpio_num_t p, rmt_item32_t *i, unsigned n)
{
	unsigned gpio = 1<<p;
	rmt_item32_t *e = i + n;
	portDISABLE_INTERRUPTS();
	uint64_t now = esp_timer_get_time(), begin = now;
	do {
		uint64_t start = now;
		GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS,gpio);
		uint16_t d0 = i->duration0, d1 = i->duration1;
		++i;
		do {
			now = esp_timer_get_time();
		} while (now-start < d0);
		GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS,gpio);
		start = now;
		do {
			now = esp_timer_get_time();
		} while (now-start < d1);
	} while (i != e);
	GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS,gpio);
	portENABLE_INTERRUPTS();
	return now-begin;
}
#endif	// ESP8266


WS2812BDrv::WS2812BDrv(rmt_channel_t ch, gpio_num_t gpio, size_t nleds)
: m_values(0)
, m_items(0)
, m_num(nleds)
, m_gpio(gpio)
, m_ch(ch)
, m_ready(false)
{
	if (gpio >= GPIO_NUM_MAX) {
		log_error(TAG,"gpio value out of range");
	}
	log_info(TAG,"constructed");
}


WS2812BDrv::~WS2812BDrv()
{
	free(m_values);
	free(m_items);
#ifdef ESP32
	rmt_driver_uninstall(m_ch);
#endif
}


bool WS2812BDrv::init()
{
	if (m_values)
		free(m_values);
	log_info(TAG,"init");
	m_values = (uint8_t*) malloc(m_num*3);
	if (m_values == 0)
		return false;
	memset(m_values,0,m_num*3);
	if (m_items)
		free(m_items);
	m_items = (rmt_item32_t*)malloc(sizeof(rmt_item32_t)*(24*m_num+1/*for reset*/));
	if (m_items == 0)
		return false;
#ifdef ESP32
	rmt_config_t rmt_tx;
	memset(&rmt_tx,0,sizeof(rmt_tx));
	rmt_tx.channel = m_ch;
	rmt_tx.gpio_num = m_gpio;
	rmt_tx.mem_block_num = 1;
	rmt_tx.clk_div = 1;
	rmt_tx.tx_config.loop_en = false;
	rmt_tx.tx_config.carrier_en = false;
	rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_HIGH;
	rmt_tx.tx_config.idle_output_en = true;
	rmt_tx.rmt_mode = RMT_MODE_TX;
	rmt_config(&rmt_tx);
	if (esp_err_t e = rmt_driver_install(m_ch, 0, 0)) {
		log_error(TAG,"rmt driver install 0x%x",e);
		return false;
	}
#else
	gpio_pad_select_gpio((gpio_num_t)m_gpio);
	if (esp_err_t e = gpio_set_direction((gpio_num_t)m_gpio, GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set %u to output: 0x%x",m_gpio,e);
		return false;
	}
#endif
	log_info(TAG,"init ok");
	m_ready = true;
	return true;
}


void WS2812BDrv::set_led(unsigned led, uint8_t r, uint8_t g, uint8_t b)
{
	if ((led >= m_num) || (!m_ready))
		return;
	uint8_t *v = m_values+led*3;
	*v++ = g;
	*v++ = r;
	*v = b;
}


void WS2812BDrv::set_led(unsigned led, uint32_t rgb)
{
	if ((led >= m_num) || (!m_ready))
		return;
	uint8_t *v = m_values+led*3;
	*v++ = (rgb >> 8) & 0xff;
	*v++ = (rgb >> 16) & 0xff;
	*v = rgb & 0xff;
}


void WS2812BDrv::set_leds(uint32_t rgb)
{
	uint8_t *v = m_values;
	uint8_t b = rgb & 0xff;
	rgb >>= 8;
	uint8_t g = rgb & 0xff;
	rgb >>= 8;
	uint8_t r = rgb & 0xff;
	unsigned l = m_num;
	do {
		*v++ = g;
		*v++ = r;
		*v++ = b;
	} while (--l);
}


void WS2812BDrv::update()
{
	if (!m_ready)
		return;
	log_info(TAG,"update0");
	assert(m_values);
	uint8_t *v = m_values, *e = m_values+m_num*3;
	assert(m_items);
	rmt_item32_t *r = m_items;
	while (v != e) {
		uint8_t l = *v++;
		for (int i = 0; i < 8; ++i) {
#ifdef ESP32
			r->level0 = 1;
			r->level1 = 0;
#endif
			if (l & 0x80) {
				r->duration0 = T1H;
				r->duration1 = T1L;
			} else {
				r->duration0 = T0H;
				r->duration1 = T0L;
			}
			l <<= 1;
			++r;
		}
		assert(r-m_items <= 24*m_num);
	}
	// add reset after data
#ifdef ESP32
	r->level0 = 0;
	r->level1 = 1;
#endif
	r->duration0 = TR;
	r->duration1 = 0;
	++r;
	assert(r-m_items <= 24*m_num+1);
#ifdef ESP32
	//log_info(TAG,"writing %u items",r-m_items);
	rmt_write_items(m_ch, m_items, r-m_items, true);
	//log_info(TAG,"update1");
	rmt_wait_tx_done(m_ch, portMAX_DELAY);
	//log_info(TAG,"update2");
#else
	uint32_t t = write_items(m_gpio, m_items, r-m_items);
	log_info(TAG,"got %u ticks",t);
#endif
}


void WS2812BDrv::reset()
{
	//log_info(TAG,"reset0");
	rmt_item32_t rst;
#ifdef ESP32
	rst.level0 = 0;
	rst.level1 = 0;
#endif
	rst.duration0 = TR;
	rst.duration1 = 0;
#ifdef ESP32
	rmt_write_items(m_ch, &rst, 1, true);
	//log_info(TAG,"reset1");
	rmt_wait_tx_done(m_ch, portMAX_DELAY);
	//log_info(TAG,"reset2");
#else
	write_items(m_gpio, &rst, 1);
#endif
}

#endif
