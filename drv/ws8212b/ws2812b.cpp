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

#include <sdkconfig.h>


#ifdef CONFIG_RGBLEDS
// CONFIG_IDF_TARGET_ESP32 rmt does not work before IDF release v3.2!!!
// CONFIG_IDF_TARGET_ESP8266 for some reason doesn't achieve the necessary performance!!!
// Placing the critical section in IRAM didn't give any benfit.

#include "log.h"
#include "ws2812b.h"

#include <stdlib.h>
#include <string.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if defined CONFIG_IDF_TARGET_ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif

#include <esp_attr.h>
#include <esp_err.h>
#include <esp_timer.h>

#define TAG MODULE_WS2812


#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
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
//#include <driver/rtc.h>
// ws2812b requirements:
// 160MHz : 6.25ns per tick
// 80MHz  : 12.5ns per tick
// t0h 0.40us +/-150ns	:  64 ticks @ 160MHz
// t0l 0.85us +/-150ns  : 136 ticks @ 160MHz
// t1h 0.80us +/-150ns  : 128 ticks @ 160MHz
// t1l 0.45us +/-150ns	:  72 ticks @ 160MHz

// ws2812b new calculation:
// 160MHz : 6.25ns per tick
// 80MHz  : 12.5ns per tick
// T0H: 400ns = 64 cycles
// T0L: 850ns = 136 cycles
// T1H: 800ns = 128 cycles
// T1L: 450ns = 72 cycles
#define T0H160	52
#define T0L160	118
#define T1H160	115
#define T1L160	58
#define TR160	8100

#define T0H80	14
#define T0L80	46
#define T1H80	44
#define T1L80	17
#define TR80	4050

#define T0H m_t0h
#define T0L m_t0l
#define T1H m_t1h
#define T1L m_t1l
#define TR m_tr
#else
#error unknwon target
#endif

WS2812BDrv *WS2812BDrv::First = 0;


#if defined CONFIG_IDF_TARGET_ESP8266

typedef struct rmt_item32_s {
	uint8_t duration0;
	uint8_t duration1;
} rmt_item32_t;


// extern linkage to force a dedicated function in IRAM
IRAM_ATTR void ws2812b_write(unsigned gpio, uint8_t *d, uint8_t *end, uint8_t t0l, uint8_t t0h, uint8_t t1l, uint8_t t1h)
{
	portENTER_CRITICAL();
	do {
		uint8_t l = *d;
		uint8_t b = 0x80;
		do {
			uint32_t s,e;
			GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS,gpio);
			asm volatile ("rsr %0, ccount" : "=r"(s));
			uint8_t tl, th;
			if (l & b) {
				tl = t1l;
				th = t1h;
			} else {
				tl = t0l;
				th = t0h;
			}
			do {
				asm volatile ("rsr %0, ccount" : "=r"(e));
			} while (e-s < th);
			GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS,gpio);
			asm volatile ("rsr %0, ccount" : "=r"(s));
			do {
				asm volatile ("rsr %0, ccount" : "=r"(e));
			} while (e-s < tl);
			b >>= 1;
		} while (b);
		++d;
	} while (d != end);
	portEXIT_CRITICAL();
}


// extern linkage to force a dedicated function in IRAM
IRAM_ATTR void ws2812b_reset(unsigned gpio, uint16_t tr)
{
	portENTER_CRITICAL();
	GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS,gpio);
	uint32_t s,e;
	asm volatile ("rsr %0, ccount" : "=r"(s));
	do {
		asm volatile ("rsr %0, ccount" : "=r"(e));
	} while (e-s < tr);
	portEXIT_CRITICAL();
}


#endif	// CONFIG_IDF_TARGET_ESP8266


int WS2812BDrv::init(gpio_num_t gpio, size_t nleds, rmt_channel_t ch)
{
	log_dbug(TAG,"init(%u,%u,%u)",gpio,nleds,ch);
#if defined CONFIG_IDF_TARGET_ESP8266
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
	if (m_set == 0) {
		log_error(TAG,"Out of memory.");
		return 1;
	}
	m_cur = m_set+nleds*3;
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	if (ch == (rmt_channel_t)-1) {
		log_warn(TAG,"channel not set");
		return 1;
	}
	m_items = (rmt_item32_t*) calloc(sizeof(rmt_item32_t),(24*nleds+1/*for reset*/));
	if (m_items == 0) {
		log_error(TAG,"Out of memory.");
		return 1;
	}
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
	if (esp_err_t e = rmt_config(&rmt_tx)) {
		log_error(TAG,"rmt config error 0x%x",e);
		return 1;
	}
	if (esp_err_t e = rmt_driver_install(m_ch, 0, 0)) {
		log_warn(TAG,"rmt driver install on channel %u: %s",m_ch,esp_err_to_name(e));
		return 1;
	}
	log_dbug(TAG,"rmt driver installed on channel %u",m_ch);
#else
	gpio_pad_select_gpio(m_gpio);
	if (esp_err_t e = gpio_set_direction(m_gpio, GPIO_MODE_OUTPUT)) {
		log_warn(TAG,"cannot set %u to output: %s",m_gpio,esp_err_to_name(e));
		return 1;
	}
	GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS,1<<m_gpio);
#endif
	m_num = nleds;
	if (m_tmr == 0)
		m_tmr = xTimerCreate("ws2812b",pdMS_TO_TICKS(20),false,(void*)this,timerCallback);
	log_dbug(TAG,"init ok");
	return 0;
}


WS2812BDrv *WS2812BDrv::get_bus(const char *n)
{
	WS2812BDrv *drv = First;
	while (drv && strcmp(drv->m_name,n))
		drv = drv->m_next;
	if (0 == drv)
		log_dbug(TAG,"invalud bus %s",n);
	return drv;
}


void WS2812BDrv::set_led(unsigned led, uint8_t r, uint8_t g, uint8_t b)
{
	if (led >= m_num) {
		log_warn(TAG,"LED inaccessible");
		return;
	}
	log_dbug(TAG,"%s: set(%u,%u,%u,%u)",m_name,led,r,g,b);
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
	log_dbug(TAG,"%s: set(%u,%06x)",m_name,led,rgb);
	uint8_t *v = m_set+led*3;
	*v++ = (rgb >> 8) & 0xff;
	*v++ = (rgb >> 16) & 0xff;
	*v = rgb & 0xff;
}


uint32_t WS2812BDrv::get_led(unsigned led)
{
	if (led >= m_num)
		return 0;
	uint8_t *v = m_set+led*3;
	uint32_t r;
	r = ((uint32_t)*v) << 8;
	++v;
	r |= ((uint32_t)*v) << 16;
	++v;
	r |= *v;
	return r;
}


void WS2812BDrv::set_leds(uint32_t rgb)
{
	log_dbug(TAG,"%s: set_leds(%06x)",m_name,rgb);
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
#if defined CONFIG_IDF_TARGET_ESP8266
	uint8_t tmp[e-v];		// move data to IRAM!
	memcpy(tmp,v,sizeof(tmp));
	ws2812b_write(1 << m_gpio,tmp,tmp+sizeof(tmp),m_t0l,m_t0h,m_t1l,m_t1h);
	ws2812b_reset(1 << m_gpio,m_tr);
#else
	assert(m_items);
	rmt_item32_t *r = m_items;
	while (v != e) {
		uint8_t l = *v++;
		for (int i = 0; i < 8; ++i) {
			r->level0 = 1;
			r->level1 = 0;
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
	r->duration0 = 0;
	r->duration1 = TR;
	r->level0 = 1;
	r->level1 = 0;
	assert(r-m_items <= 24*m_num+1);
	//log_info(TAG,"writing %u items",r-m_items);
	rmt_write_items(m_ch, m_items, m_num*24+1, false);
#endif
}


void WS2812BDrv::reset()
{
	//log_info(TAG,"reset0");
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	rmt_item32_t rst;
	rst.level0 = 0;
	rst.level1 = 0;
	rst.duration0 = TR;
	rst.duration1 = 0;
	rmt_write_items(m_ch, &rst, 1, true);
#else
	ws2812b_reset(1 << m_gpio,m_tr);
#endif
}

#endif
