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

#ifdef CONFIG_NIGHTSKY

#include "actions.h"
#include "binformats.h"
#include "cyclic.h"
#include "globals.h"
#include "log.h"
#include "settings.h"
#include "terminal.h"
#include "tlc5947.h"

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

//#define MAX_BRIGHT ((1 << 12)-1)
#define MAX_BRIGHT (1 << 11)


static const char TAG[] = "nightsky";

static TLC5947 Drv;
static uint16_t NumLed, *Values;
static uint8_t *Slope;
static unsigned Interval, LastUpdate;
//static const uint16_t LumMap[] = { 1<<3, 1<<4, 1<<5, 1<<6, 1<<7, 1<<8, 1<<9, 1<<10, 1<<11, (1<<12)-1 };
static const uint16_t LumMap[] = { 1<<4, 1<<5, 1<<6, 1<<7, 1<<8, 1<<9, 1<<10, 1<<11 };


static unsigned nightsky_step(void *)
{
	bool delta = false;
	if (Interval == 0)
		return 500;
	for (int i = 0; i < NumLed; ++i) {
		uint16_t v = Drv.get_led(i);
		int d = Values[i] - v;
		int16_t s;
		if (d < 0) {
			d = -d;
			s = -1;
		} else if (d > 0) {
			s = 1;
		} else
			continue;
		delta = true;
		if (d > Slope[i])
			s *= Slope[i];
		else 
			s *= d;
		Drv.set_led(i,v+s);
	}
	if (delta) {
		Drv.commit();
		return Interval;
	}
	return 300;
}


static void nightsky_random(void*)
{
	log_info(TAG,"random()");
	for (int i = 0; i < NumLed; ++i) {
		uint32_t r = esp_random();
		Values[i] = LumMap[r%(sizeof(LumMap)/sizeof(LumMap[0]))];
		Slope[i] = r % 60 + 30;
	}
}


static void nightsky_toggle(void*)
{
	log_info(TAG,"toggle()");
	if (Drv.is_on())
		Drv.off();
	else
		Drv.on();
}


static void nightsky_off(void*)
{
	log_info(TAG,"off()");
	Drv.off();
}


static void nightsky_on(void*)
{
	log_info(TAG,"on()");
	Drv.on();
}


static void nightsky_fade(void*p)
{
	unsigned v = (unsigned) p;
	log_dbug(TAG,"fade(%u)",v);
	for (int i = 0; i < NumLed; ++i)
		Values[i] = v;
}


static unsigned nightsky_update(void*)
{
	// update every couple of minutes with a new random set
	unsigned itv;
	if (cfg_get_uvalue("nightsky_randomitv",&itv,0) || (itv == 0))
		return 500;
	uint64_t now = esp_timer_get_time()/1000;
	if ((LastUpdate+itv*1000) < now) {
		nightsky_random(0);
		LastUpdate = now;
	}
	return 200;
}


int nightsky(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("ns {on|off|max|random|list}\nns interval <ms>\nns <led> <value>\nns <value>\n");
		term.printf("slope:");
		for (int i = 0; i < NumLed; ++i)
			term.printf(" %u",Slope[i]);
		term.printf("\n");
		return 0;
	}
	if (!strcmp(args[1],"max")) {
		nightsky_fade((void*)MAX_BRIGHT);
		return 0;
	}
	if (!strcmp(args[1],"min")) {
		nightsky_fade(0);
		return 0;
	}
	if (!strcmp(args[1],"list")) {
		term.printf(" value  slope\n");
		for (int i = 0; i < NumLed; ++i)
			term.printf("%6u %3u\n",Values[i],Slope[i]);
		return 0;
	}
	if (!strcmp(args[1],"random")) {
		nightsky_random(0);
		return 0;
	}
	if (!strcmp(args[1],"on")) {
		nightsky_on(0);
		return 0;
	}
	if (!strcmp(args[1],"off")) {
		nightsky_off(0);
		return 0;
	}
	long l = strtol(args[1],0,0);
	if (argc == 2) {
		if (!strcmp(args[1],"interval")) {
			term.printf("interval %u\n",Interval);
			return 0;
		}
		if ((args[1][0] < '0') || (args[1][0] > '9'))
			return 1;
		if ((l < 0) || (l > MAX_BRIGHT)) {
			term.printf("value out of range\n");
			return 1;
		}
		nightsky_fade((void*)l);
		return 0;
	}
	if (!strcmp(args[1],"interval")) {
		l = strtol(args[2],0,0);
		if (l < 0) {
			term.printf("interval out of range");
			return 1;
		}
		term.printf("interval %ld\n",l);
		Interval = l;
		return 0;
	}
	if (!strcmp(args[1],"slope")) {
		l = strtol(args[2],0,0);
		if ((l < 0) || (l > MAX_BRIGHT)){
			term.printf("slope out of range");
			return 1;
		}
		term.printf("slope %ld\n",l);
		for (int i = 0; i < NumLed; ++i)
			Slope[i] = l;
		return 0;
	}
	if (argc == 2) {
		if ((l < 0) || (l >= NumLed)) {
			term.printf("led out of range\n");
			return 1;
		}
		int led = l;
		l = strtol(args[1],0,0);
		if ((l < 0) || (l >= (1<<12))) {
			term.printf("value out of range\n");
			return 1;
		}
		Values[led] = l;
		return 0;
	}
	return 1;
}


int nightsky_setup()
{
	const Tlc5947Config &c = HWConf.tlc5947();
	if (!c.has_sin() || !c.has_sclk() || !c.has_xlat() || !c.has_blank() || (0 == c.ntlc())) {
		log_info(TAG,"not configured");
		return 0;
	}
	int r = Drv.init
		( (gpio_num_t)c.sin()
		, (gpio_num_t)c.sclk()
		, (gpio_num_t)c.xlat()
		, (gpio_num_t)c.blank()
		, c.ntlc());
	if (r)
		return 1;
	Drv.off();
	Drv.commit();
	NumLed = c.ntlc() * 24;
	Values = (uint16_t *) malloc(NumLed*sizeof(Values[0]));
	Slope = (uint8_t *) malloc(NumLed*sizeof(Slope[0]));
	nightsky_random(0);
	Drv.on();
	unsigned itv;
	if (cfg_get_uvalue("nightsky_randomitv",&itv,0))
		cfg_set_uvalue("nightsky_randomitv",0);
	if (cfg_get_uvalue("nightsky_interval",&Interval,40))
		cfg_set_uvalue("nightsky_interval",40);
	action_add("nightsky_on",nightsky_on,0,"turn on nightsky");
	action_add("nightsky_off",nightsky_off,0,"turn off nightsky");
	action_add("nightsky_toggle",nightsky_toggle,0,"toggle on/off nightsky");
	action_add("nightsky_max",nightsky_fade,(void*)(MAX_BRIGHT),"all stars maximum brightness");
	action_add("nightsky_min",nightsky_fade,0,"all stars dark");
	action_add("nightsky_random",nightsky_random,0,"randomize stars' brightness");
	int e = cyclic_add_task("nightsky_step",nightsky_step);
	if (e != 0)
		return e;
	e = cyclic_add_task("nightsky_update",nightsky_update);
	if (e == 0)
		log_info(TAG,"setup done");
	return e;
}


#endif
