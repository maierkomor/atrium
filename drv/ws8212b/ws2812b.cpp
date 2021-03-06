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

#ifdef CONFIG_LEDSTRIP
// CONFIG_IDF_TARGET_ESP32 rmt does not work before IDF release v3.2!!!
// CONFIG_IDF_TARGET_ESP8266 for some reason doesn't achieve the necessary performance!!!
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


#if defined CONFIG_IDF_TARGET_ESP32
// CONFIG_IDF_TARGET_ESP32 at 80MHz, numbers in cycles
// OK!
#define T0H	28	// 0.35us
#define T0L	64	// 0.80us
#define T1H	56	// 0,70us
#define T1L	48	// 0.60us
#define TR	4400	// >50us=4000
#elif defined CONFIG_IDF_TARGET_ESP8266
extern "C" {
#include <esp_clk.h>
}
#include <driver/rtc.h>
//// 160MHz values - warum hat das mal funktioniert?! commit 6b3ae96eea4d
//#define T0H160	2
//#define T0L160	6
//#define T1H160	6
//#define T1L160	2
//#define TR160	500
// 160MHz values
#define T0H160	8
#define T0L160	20
#define T1H160	20
#define T1L160	8
#define TR160	1500
// 80MHz values
#define T0H80	4
#define T0L80	10
#define T1H80	10
#define T1L80	4
#define TR80	800
// ws2812b requirements:
// t0h 0.40us +/-150ns	:  64 ticks @ 160MHz
// t0l 0.85us +/-150ns  : 136 ticks @ 160MHz
// t1h 0.80us +/-150ns  : 128 ticks @ 160MHz
// t1l 0.45us +/-150ns	:  72 ticks @ 160MHz
#else
#error unknwon target
#endif

#ifdef CONFIG_IDF_TARGET_ESP8266

typedef struct rmt_item32_s {
	uint8_t duration0;
	uint8_t duration1;
} rmt_item32_t;


static inline IRAM_ATTR void delay_iter(volatile unsigned x)
{
	// for some reason execution time is not deterministic
	while (x)
		--x;
}


static IRAM_ATTR void write_items(gpio_num_t p, rmt_item32_t *i, unsigned n)
{
	unsigned gpio = 1<<p;
	rmt_item32_t *e = i + n;
	portENTER_CRITICAL();
	do {
		GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS,gpio);
		delay_iter(i->duration0);
		GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS,gpio);
		delay_iter(i->duration1);
		++i;
	} while (i != e);
	portEXIT_CRITICAL();
}
#endif	// CONFIG_IDF_TARGET_ESP8266


int WS2812BDrv::init(gpio_num_t gpio, size_t nleds, rmt_channel_t ch)
{
#ifdef CONFIG_IDF_TARGET_ESP8266
	int f = esp_clk_cpu_freq();
	if (f == 160000000) {
		m_t0l = T0L160;
		m_t0h = T0H160;
		m_t1l = T1L160;
		m_t1h = T1H160;
		m_tr  = TR160;
	} else if (f == 80000000) {
		m_t0l = T0L80;
		m_t0h = T0H80;
		m_t1l = T1L80;
		m_t1h = T1H80;
		m_tr  = TR80;
	} else {
		log_error(TAG,"unsupported CPU frequency %u",f);
		return 1;
	}
#endif
	m_num = 0;
	m_gpio = gpio;
	m_set = (uint8_t*) calloc(1,nleds*3*2);
	m_cur = m_set+nleds*3;
	m_items = (rmt_item32_t*) calloc(sizeof(rmt_item32_t),(24*nleds+1/*for reset*/));
	if ((m_set == 0) || (m_items == 0)) {
		log_error(TAG,"out of memory");
		return 1;
	}
#ifdef CONFIG_IDF_TARGET_ESP32
	m_ch = ch;
	rmt_config_t rmt_tx;
	memset(&rmt_tx,0,sizeof(rmt_tx));
	rmt_tx.channel = m_ch;
	rmt_tx.gpio_num = m_gpio;
	rmt_tx.mem_block_num = 1;
	rmt_tx.clk_div = 1;
	rmt_tx.tx_config.loop_en = false;
	rmt_tx.tx_config.carrier_en = false;
	rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
	rmt_tx.tx_config.idle_output_en = true;
	rmt_tx.rmt_mode = RMT_MODE_TX;
	rmt_config(&rmt_tx);
	if (esp_err_t e = rmt_driver_install(m_ch, 0, 0)) {
		log_error(TAG,"rmt driver install 0x%x",e);
		return 1;
	}
#else
	gpio_pad_select_gpio(m_gpio);
	if (esp_err_t e = gpio_set_direction(m_gpio, GPIO_MODE_OUTPUT)) {
		log_error(TAG,"cannot set %u to output: 0x%x",m_gpio,e);
		return 1;
	}
	GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS,1<<m_gpio);
#endif
	m_num = nleds;
	if (m_tmr == 0)
		m_tmr = xTimerCreate("ws2812b",pdMS_TO_TICKS(20),false,(void*)this,timerCallback);
	log_info(TAG,"init ok");
	return 0;
}


void WS2812BDrv::set_led(unsigned led, uint8_t r, uint8_t g, uint8_t b)
{
	if (led >= m_num) {
		log_warn(TAG,"LED inaccessible");
		return;
	}
	log_info(TAG,"set(%u,%u,%u,%u)",led,r,g,b);
	uint8_t *v = m_set+led*3;
	*v++ = g;
	*v++ = r;
	*v = b;
}


void WS2812BDrv::set_led(unsigned led, uint32_t rgb)
{
	if (led >= m_num) {
		log_warn(TAG,"LED inaccessible");
		return;
	}
	log_info(TAG,"set(%u,%06x)",led,rgb);
	uint8_t *v = m_set+led*3;
	*v++ = (rgb >> 8) & 0xff;
	*v++ = (rgb >> 16) & 0xff;
	*v = rgb & 0xff;
}


void WS2812BDrv::set_leds(uint32_t rgb)
{
	//log_info(TAG,"set(%06x)",rgb);
	uint8_t *v = m_set;
	uint8_t b = rgb & 0xff;
	rgb >>= 8;
	uint8_t g = rgb & 0xff;
	rgb >>= 8;
	uint8_t r = rgb & 0xff;
	unsigned l = m_num;
	while (l) {
		*v++ = g;
		*v++ = r;
		*v++ = b;
		--l;
	}
}


void WS2812BDrv::timerCallback(void *h)
{
	WS2812BDrv *d = (WS2812BDrv *)pvTimerGetTimerID(h);
	bool remain = false;
	uint8_t *c = d->m_cur, *e = c+d->m_num*3, *s = d->m_set;
	while (c != e) {
		int delta = *c - *s;
		if (delta < -10)
			delta = -10;
		else if (delta > 10)
			delta = 10;
		*c -= delta;
		if (*c != *s)
			remain = true;
		++c;
		++s;
	}
	d->commit();
	if (remain)
		xTimerStart(d->m_tmr,pdMS_TO_TICKS(20));
}


void WS2812BDrv::update(bool fade)
{
	if (fade) {
		xTimerStart(m_tmr,1);
	} else {
		memcpy(m_cur,m_set,m_num*3);
		commit();
	}
}


void WS2812BDrv::commit()
{
	if (0 == m_num)
		return;
	//log_info(TAG,"update0");
	assert(m_set);
	uint8_t *v = m_cur, *e = m_cur+m_num*3;
	assert(m_items);
	rmt_item32_t *r = m_items;
	while (v != e) {
		uint8_t l = *v++;
		for (int i = 0; i < 8; ++i) {
#ifdef CONFIG_IDF_TARGET_ESP32
			r->level0 = 1;
			r->level1 = 0;
#endif
			if (l & 0x80) {
#ifdef CONFIG_IDF_TARGET_ESP32
				r->duration0 = T1H;
				r->duration1 = T1L;
#else
				r->duration0 = m_t1h;
				r->duration1 = m_t1l;
#endif
			} else {
#ifdef CONFIG_IDF_TARGET_ESP32
				r->duration0 = T0H;
				r->duration1 = T0L;
#else
				r->duration0 = m_t0h;
				r->duration1 = m_t0l;
#endif
			}
			l <<= 1;
			++r;
		}
		assert(r-m_items <= 24*m_num);
	}
	// add reset after data
	r->duration0 = 0;
#ifdef CONFIG_IDF_TARGET_ESP8266
	r->duration1 = m_tr;
#elif defined CONFIG_IDF_TARGET_ESP32
	r->duration1 = TR;
	r->level0 = 1;
	r->level1 = 0;
#endif
	assert(r-m_items <= 24*m_num+1);
	//log_info(TAG,"writing %u items",r-m_items);
#ifdef CONFIG_IDF_TARGET_ESP32
	rmt_write_items(m_ch, m_items, m_num*24+1, true);
#else
	write_items(m_gpio, m_items, m_num*24+1);
#endif
}


void WS2812BDrv::reset()
{
	//log_info(TAG,"reset0");
	rmt_item32_t rst;
#ifdef CONFIG_IDF_TARGET_ESP32
	rst.level0 = 0;
	rst.level1 = 0;
	rst.duration0 = TR;
#elif defined CONFIG_IDF_TARGET_ESP8266
	rst.duration0 = m_tr;
#endif
	rst.duration1 = 0;
	//log_info(TAG,"reset");
#ifdef CONFIG_IDF_TARGET_ESP32
	rmt_write_items(m_ch, &rst, 1, true);
#else
	write_items(m_gpio, &rst, 1);
#endif
}

#endif
