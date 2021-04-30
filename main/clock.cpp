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

#ifdef CONFIG_CLOCK

#include "actions.h"
#include "binformats.h"
#include "cyclic.h"
//#include "event.h"
#include "globals.h"
#include "log.h"
#include "MAX7219.h"
#include "timefuse.h"
#include "ujson.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>

#include <sys/time.h>
#include <time.h>

typedef enum clockmode {
	cm_time,
	cm_date,
	cm_stopwatch,
	cm_temperature,
	cm_humidity,
	cm_pressure,
	cm_display_bin,	// display something - e.g. alarm set value, binary data
	cm_display_dec,	// display something - e.g. alarm set value, decimal data
	CLOCK_MODE_MAX,
} clockmode_t;


struct Clock
{
	MAX7219Drv drv;
	uint32_t sw_start = 0, sw_delta = 0;
	uint8_t digits, display[8];
	uint32_t datestart = 0;
	clockmode_t mode = cm_time;
	bool modech = false;

	void display_float(float);
	void display_time();
	void display_date();
	void display_data();
	void display_sw();
	void clearscr();
};


static const char TAG[] = "clock";
static Clock *ctx = 0;

static void sw_start(void *arg)
{
	ctx->sw_start = uptime();
	ctx->sw_delta = 0;
}


static void sw_stop(void *arg)
{
	uint32_t now = uptime();
	ctx->sw_delta = now - ctx->sw_start;
	ctx->sw_start = 0;
}


static void sw_cont(void *arg)
{
	Clock *ctx = (Clock *)arg;
	uint32_t now = uptime();
	ctx->sw_start = now - ctx->sw_delta;
	ctx->sw_delta = 0;
}


static void switch_mode(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	if (ctx->mode == cm_temperature) {
		if (0 == RTData->get("temperature"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_humidity) {
		if (0 == RTData->get("humidity"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_pressure) {
		if (0 == RTData->get("pressure"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode >= cm_display_bin)
		ctx->mode = cm_time;
	if (ctx->mode == cm_date)
		ctx->datestart = uptime();
	ctx->modech = true;
}


static void temp_mode(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->mode = cm_temperature;
	ctx->modech = true;
}


static void time_mode(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->mode = cm_time;
	ctx->modech = true;
}


static void bright_dec(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->drv.setIntensity(ctx->drv.getIntensity()-1);
}


static void bright_inc(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->drv.setIntensity(ctx->drv.getIntensity()+1);
}


static void shutdown(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->drv.shutdown();
}

static void poweron(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->drv.powerup();
}


void Clock::display_time()
{
	uint8_t h=0,m=0,s=0;
	get_time_of_day(&h,&m,&s);
	if (digits >= 6) {
		drv.setDigit(5,h/10);
		drv.setDigit(4,h%10);
		drv.setDigit(3,m/10);
		drv.setDigit(2,m%10);
		drv.setDigit(1,s/10);
		drv.setDigit(0,s%10);
	} else if (digits == 4) {
		drv.setDigit(3,h/10);
		drv.setDigit(2,h%10);
		drv.setDigit(1,m/10);
		drv.setDigit(0,m%10);
	}
}


void Clock::display_date()
{
	uint8_t wd=0,day=0,mon=0;
	unsigned year=0;
	get_time_of_day(0,0,0,&wd,&day,&mon,&year);
	unsigned digit = digits;
	drv.setDigit(--digit,day/10);
	drv.setDigit(--digit,day%10);
	drv.setDigit(--digit,mon/10);
	drv.setDigit(--digit,mon%10);
	if (digit == 4) {
		drv.setDigit(--digit,year/1000);
		drv.setDigit(--digit,(year%1000)/100);
	}
	if (digit == 2) {
		year %= 100;
		drv.setDigit(1,year/10);
		drv.setDigit(0,year%10);
	}
}


void Clock::display_sw()
{
	uint32_t dt;
	if (ctx->sw_start) {
		uint32_t now = uptime();
		dt = now - ctx->sw_start;
	} else {
		dt = ctx->sw_delta;
	}
	dt /= 10;
	uint8_t hsec = dt%10;
	dt /= 10;
	uint8_t dsec = dt%10;
	dt /= 10; // now in sec
	uint8_t h = dt / 3600;
	dt -= h * 3600;
	uint8_t m = dt / 60;
	dt -= m * 60;
	uint8_t s = dt;
	if (digits == 8) {
		ctx->drv.setDigit(digits-1,h/10);
		ctx->drv.setDigit(digits-2,h%10);
		ctx->drv.setDigit(digits-3,m/10);
		ctx->drv.setDigit(digits-4,m%10);
		ctx->drv.setDigit(digits-5,s/10);
		ctx->drv.setDigit(digits-6,s%10);
		ctx->drv.setDigit(digits-7,dsec);
		ctx->drv.setDigit(digits-8,hsec);
	} else if (digits == 6) {
		if (h) {
			ctx->drv.setDigit(digits-1,h/10);
			ctx->drv.setDigit(digits-2,h%10);
			ctx->drv.setDigit(digits-3,m/10);
			ctx->drv.setDigit(digits-4,m%10);
			ctx->drv.setDigit(digits-5,s/10);
			ctx->drv.setDigit(digits-6,s%10);
		} else {
			ctx->drv.setDigit(digits-1,m/10);
			ctx->drv.setDigit(digits-2,m%10);
			ctx->drv.setDigit(digits-3,s/10);
			ctx->drv.setDigit(digits-4,s%10);
			ctx->drv.setDigit(digits-5,dsec);
			ctx->drv.setDigit(digits-6,hsec);
		}
	} else if (h != 0) {
		ctx->drv.setDigit(digits-1,h/10);
		ctx->drv.setDigit(digits-2,h%10);
		ctx->drv.setDigit(digits-3,m/10);
		ctx->drv.setDigit(digits-4,m%10);
	} else if (m != 0) {
		ctx->drv.setDigit(digits-1,m/10);
		ctx->drv.setDigit(digits-2,m%10);
		ctx->drv.setDigit(digits-3,s/10);
		ctx->drv.setDigit(digits-4,s%10);
	} else {
		ctx->drv.setDigit(digits-1,s/10);
		ctx->drv.setDigit(digits-2,s%10);
		ctx->drv.setDigit(digits-3,dsec);
		ctx->drv.setDigit(digits-4,hsec);
	}
}


void Clock::display_data()
{
	for (int i = 0; i < digits; ++i)
		ctx->drv.setDigit(ctx->digits-i-1,ctx->display[i]);
}


void Clock::display_float(float f)
{
	char buf[8];
	sprintf(buf,"%3.1f",f);
	char *b = buf;
	unsigned digit = 8;
	while (*b) {
		uint8_t v = (*b++ - '0');
		if (*b == '.') {
			v |= 0x80;
			++b;
		}
		drv.setDigit(--digit,v);
	}
}


void Clock::clearscr()
{
	for (int d = 0; d < digits; ++d)
		drv.setDigit(d,0);
}


static unsigned clock_iter(void *arg)
{
	Clock *ctx = (Clock *)arg;
	if (ctx->modech) {
		ctx->clearscr();
		switch (ctx->mode) {
		case cm_display_bin:
			ctx->drv.setDecoding(0x0);
			break;
		case cm_time:
			ctx->drv.setDecoding(0x3f);
			break;
		case cm_date:
			ctx->drv.setDecoding(0xff);
			break;
		case cm_temperature:
			ctx->drv.setDecoding(0xf0);
			ctx->drv.setDigit(2,0x63);	// Degree
			ctx->drv.setDigit(1,0x4e);
			break;
		case cm_humidity:
			ctx->drv.setDecoding(0xf0);
			ctx->drv.setDigit(2,0x63);	// Percent
			ctx->drv.setDigit(1,0x1d);
			break;
		case cm_pressure:
			ctx->drv.setDecoding(0xf0);
			ctx->drv.setDigit(2,0x67);	// P
			ctx->drv.setDigit(1,0x77);	// A
			break;
		case cm_display_dec:
		case cm_stopwatch:
			ctx->drv.setDecoding(0xff);
			break;
		default:
			abort();
		}
		ctx->modech = false;
	}
	unsigned d = 100;
	switch (ctx->mode) {
	case cm_date:
		if ((uptime() - ctx->datestart) < 3000) {
			ctx->display_date();
			d = 500;
			break;
		}
		log_dbug(TAG,"switch back");
		ctx->mode = cm_time;
		ctx->modech = true;
		d = 20;
		break;
	case cm_time:
		ctx->display_time();
		break;
	case cm_stopwatch:
		ctx->display_sw();
		d = 20;
		break;
	case cm_temperature:
		ctx->display_float(RTData->get("temperature")->toNumber()->get());
		break;
	case cm_humidity:
		ctx->display_float(RTData->get("humidity")->toNumber()->get());
		break;
	case cm_pressure:
		ctx->display_float(RTData->get("pressure")->toNumber()->get());
		break;
	case cm_display_bin:
	case cm_display_dec:
		ctx->display_data();
		break;
	default:
		abort();
	}
	return d;
}


int clockapp_setup()
{
	if (!HWConf.has_max7219()) {
		log_dbug(TAG,"not configured");
		return 0;
	}
	// 5V level adjustment necessary
	// ESP8266 is not capable of driving directly
	// ESP32 seems to work
	const Max7219Config &c = HWConf.max7219();
	if (!c.has_clk() || !c.has_dout() || !c.has_cs() || !c.has_digits()) {
		log_warn(TAG,"incomplete config");
		return 1;
	}
	ctx = new Clock;
	ctx->digits = c.digits();
	if (ctx->drv.init((gpio_num_t)c.clk(),(gpio_num_t)c.dout(),(gpio_num_t)c.cs(),c.odrain())) {
		delete ctx;
		return 1;
	}
	action_add("clock!switch_mode",switch_mode,ctx,"switch to next clock mode");
	action_add("clock!time_mode",time_mode,ctx,"switch to display time display");
	action_add("clock!temp_mode",temp_mode,(void*)ctx,"switch to display temperature");
	action_add("clock!bright_inc",bright_inc,(void*)ctx,"clock: increase brightness");
	action_add("clock!bright_dec",bright_dec,(void*)ctx,"clock: decrease brightness");
	action_add("clock!off",shutdown,ctx,"clock: turn off");
	action_add("clock!on",poweron,ctx,"clock: turn on");
	action_add("sw!start",sw_start,ctx,"stopwatch start");
	action_add("sw!stop",sw_stop,ctx,"stopwatch stop");
	action_add("sw!resume",sw_cont,ctx,"stopwatch resume");
	shutdown(ctx);
	ctx->drv.setDigits(ctx->digits);
	poweron(ctx);
	ctx->drv.displayTest(true);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	ctx->drv.displayTest(false);
	ctx->drv.setIntensity(0xf);
	ctx->mode = cm_time;
	ctx->modech = true;
	ctx->clearscr();
	log_info(TAG,"got first time sample");
	cyclic_add_task("clock", clock_iter, ctx);
	return 0;
}

#endif
