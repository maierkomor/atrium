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
#include "event.h"
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

static const char TAG[] = "clock";
static MAX7219Drv Driver;
static uint32_t Start, Delta;
static uint8_t Digits, Display[8];
static clockmode_t Mode;
static event_t DateStartEv,DateTimeoutEv;


static void sw_start(void *arg)
{
	Start = uptime();
	Delta = 0;
}


static void sw_stop(void *arg)
{
	uint32_t now = uptime();
	Delta = now - Start;
	Start = 0;
}


static void sw_cont(void *arg)
{
	uint32_t now = uptime();
	Start = now - Delta;
	Delta = 0;
}


static void display(void *arg)
{
	if (arg == 0)
		return;
	memcpy(Display,arg,Digits);
}


static void switch_mode(void *arg)
{
	int m = (int)arg;
	if (m == -1)
		Mode = (clockmode_t)((int)Mode + 1);
	else
		Mode = (clockmode_t)m;
	if (Mode == cm_temperature) {
		if (Temperature == 0)
			Mode = (clockmode_t)((int)Mode + 1);
	}
	if (Mode == cm_humidity) {
		if (Humidity == 0)
			Mode = (clockmode_t)((int)Mode + 1);
	}
	if (Mode == cm_pressure) {
		if (Humidity == 0)
			Mode = (clockmode_t)((int)Mode + 1);
	}
	if (Mode >= cm_display_bin)
		Mode = cm_time;
	if (Mode == cm_date)
		event_trigger(DateStartEv);
}


static void switch_back(void *arg)
{
	log_dbug(TAG,"switch back");
	if (Mode == cm_date)
		Mode = cm_time;
}


static void bright_update(void *arg)
{
	int d = (int) arg;
	Driver.setIntensity(Driver.getIntensity()+d);
}


static void shutdown(void *arg=0)
{
	Driver.shutdown();
}

static void poweron(void *arg=0)
{
	Driver.powerup();
}


static unsigned display_time()
{
	uint8_t h=0,m=0,s=0;
	get_time_of_day(&h,&m,&s);
	if (Digits >= 6) {
		Driver.setDigit(5,h/10);
		Driver.setDigit(4,h%10);
		Driver.setDigit(3,m/10);
		Driver.setDigit(2,m%10);
		Driver.setDigit(1,s/10);
		Driver.setDigit(0,s%10);
	} else if (Digits == 4) {
		Driver.setDigit(3,h/10);
		Driver.setDigit(2,h%10);
		Driver.setDigit(1,m/10);
		Driver.setDigit(0,m%10);
	}
	return 100;
}


static unsigned display_date()
{
	uint8_t wd=0,day=0,mon=0;
	unsigned year=0;
	get_time_of_day(0,0,0,&wd,&day,&mon,&year);
	unsigned digit = Digits;
	Driver.setDigit(--digit,day/10);
	Driver.setDigit(--digit,day%10);
	Driver.setDigit(--digit,mon/10);
	Driver.setDigit(--digit,mon%10);
	if (digit == 4) {
		Driver.setDigit(--digit,year/1000);
		Driver.setDigit(--digit,(year%1000)/100);
	}
	if (digit == 2) {
		year %= 100;
		Driver.setDigit(1,year/10);
		Driver.setDigit(0,year%10);
	}
	return 500;
}


static unsigned display_sw()
{
	uint32_t dt;
	if (Start) {
		uint32_t now = uptime();
		dt = now - Start;
	} else {
		dt = Delta;
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
	if (Digits == 8) {
		Driver.setDigit(Digits-1,h/10);
		Driver.setDigit(Digits-2,h%10);
		Driver.setDigit(Digits-3,m/10);
		Driver.setDigit(Digits-4,m%10);
		Driver.setDigit(Digits-5,s/10);
		Driver.setDigit(Digits-6,s%10);
		Driver.setDigit(Digits-7,dsec);
		Driver.setDigit(Digits-8,hsec);
	} else if (Digits == 6) {
		if (h) {
			Driver.setDigit(Digits-1,h/10);
			Driver.setDigit(Digits-2,h%10);
			Driver.setDigit(Digits-3,m/10);
			Driver.setDigit(Digits-4,m%10);
			Driver.setDigit(Digits-5,s/10);
			Driver.setDigit(Digits-6,s%10);
		} else {
			Driver.setDigit(Digits-1,m/10);
			Driver.setDigit(Digits-2,m%10);
			Driver.setDigit(Digits-3,s/10);
			Driver.setDigit(Digits-4,s%10);
			Driver.setDigit(Digits-5,dsec);
			Driver.setDigit(Digits-6,hsec);
		}
	} else if (h != 0) {
		Driver.setDigit(Digits-1,h/10);
		Driver.setDigit(Digits-2,h%10);
		Driver.setDigit(Digits-3,m/10);
		Driver.setDigit(Digits-4,m%10);
	} else if (m != 0) {
		Driver.setDigit(Digits-1,m/10);
		Driver.setDigit(Digits-2,m%10);
		Driver.setDigit(Digits-3,s/10);
		Driver.setDigit(Digits-4,s%10);
	} else {
		Driver.setDigit(Digits-1,s/10);
		Driver.setDigit(Digits-2,s%10);
		Driver.setDigit(Digits-3,dsec);
		Driver.setDigit(Digits-4,hsec);
	}
	return 20;
}


static unsigned display_data()
{
	for (int i = 0; i < Digits; ++i)
		Driver.setDigit(Digits-i-1,Display[i]);
	return 100;
}


static void display_float(float f)
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
		Driver.setDigit(--digit,v);
	}
}


static unsigned display_temperature()
{
	float f = Temperature->get();
	display_float(f);
	return 100;
}


static unsigned display_humidity()
{
	float f = Humidity->get();
	display_float(f);
	return 100;
}


static unsigned display_pressure()
{
	float f = Pressure->get();
	display_float(f);
	return 100;
}


static void clearscr()
{
	for (int d = 0; d < Digits; ++d)
		Driver.setDigit(d,0);
}


static void clock_task(void *ignored)
{
	log_info(TAG,"started");
	shutdown();
	Driver.setDigits(Digits);
	poweron();
	Driver.displayTest(true);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	Driver.displayTest(false);
	Driver.setIntensity(0xf);
	Mode = cm_time;
	clockmode_t mode = CLOCK_MODE_MAX;
	clearscr();
	log_info(TAG,"got first time sample");
	for (;;) {
		if (Mode != mode) {
			clearscr();
			switch (Mode) {
			case cm_display_bin:
				Driver.setDecoding(0x0);
				break;
			case cm_time:
				Driver.setDecoding(0x3f);
				break;
			case cm_date:
				Driver.setDecoding(0xff);
				break;
			case cm_temperature:
				Driver.setDecoding(0xf0);
				Driver.setDigit(2,0x63);	// Degree
				Driver.setDigit(1,0x4e);
				break;
			case cm_humidity:
				Driver.setDecoding(0xf0);
				Driver.setDigit(2,0x63);	// Percent
				Driver.setDigit(1,0x1d);
				break;
			case cm_pressure:
				Driver.setDecoding(0xf0);
				Driver.setDigit(2,0x67);	// P
				Driver.setDigit(1,0x77);	// A
				break;
			case cm_display_dec:
			case cm_stopwatch:
				Driver.setDecoding(0xff);
				break;
			default:
				abort();
			}
			mode = Mode;
		}
		unsigned d;
		switch (mode) {
		case cm_time:
			d = display_time();
			break;
		case cm_date:
			d = display_date();
			break;
		case cm_stopwatch:
			d = display_sw();
			break;
		case cm_temperature:
			d = display_temperature();
			break;
		case cm_humidity:
			d = display_humidity();
			break;
		case cm_pressure:
			d = display_pressure();
			break;
		case cm_display_bin:
		case cm_display_dec:
			d = display_data();
			break;
		default:
			abort();
		}
		vTaskDelay(d/portTICK_PERIOD_MS);
	}
}


int clockapp_setup()
{
	if (!HWConf.has_max7219()) {
		log_info(TAG,"not configured");
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
	Digits = c.digits();
	if (Driver.init((gpio_num_t)c.clk(),(gpio_num_t)c.dout(),(gpio_num_t)c.cs(),c.odrain()))
		return 1;
	action_add("clock!switch_mode",switch_mode,(void*)-1,"switch to next clock mode");
	action_add("clock!clock_mode",switch_mode,(void*)0,"switch to display time clock mode");
	action_add("clock!temp_mode",switch_mode,(void*)cm_temperature,"switch to display temperature");
	action_add("clock!bright_inc",bright_update,(void*)1,"clock: increase brightness");
	action_add("clock!bright_dec",bright_update,(void*)-1,"clock: decrease brightness");
	action_add("clock!off",shutdown,0,"clock: turn off");
	action_add("clock!on",poweron,0,"clock: turn on");
	action_add("clock!display",display,0,"clock: display value (arg: uint8_t[Digits])");
	action_add("sw!start",sw_start,0,"stopwatch start");
	action_add("sw!stop",sw_stop,0,"stopwatch stop");
	action_add("sw!resume",sw_cont,0,"stopwatch resume");
	timefuse_t t = timefuse_create("clock_mode_timer",3000);
	DateTimeoutEv = timefuse_timeout_event(t);
	DateStartEv = event_register("clock`date_entered");
	Action *a = action_add("clock!mode_back", switch_back, 0,  "switch back from date to time mode");
	event_callback(DateTimeoutEv,a);
	event_callback(DateStartEv,action_get("clock_mode_timer!start"));
	BaseType_t r = xTaskCreate(&clock_task, "clock", 2048, NULL, 4, NULL);
	if (r != pdPASS) {
		log_error(TAG,"error starting task: %d",r);
		return 1;
	}
	return 0;
}

#endif
