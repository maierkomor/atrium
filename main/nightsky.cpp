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

#ifdef CONFIG_TLC5947

#include "actions.h"
#include "hwcfg.h"
#include "cyclic.h"
#include "globals.h"
#include "log.h"
#include "settings.h"
#include "terminal.h"
#include "tlc5947.h"

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

//#define MAX_BRIGHT ((1 << 12)-1)
#define MAX_BRIGHT (1 << 11)


#define TAG MODULE_NIGHTSKY

static TLC5947 *Drv = 0;


#if 0
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
		log_dbug(TAG,"not configured");
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
#ifdef CONFIG_APP_PARAMS 
	unsigned itv;
	if (cfg_get_uvalue("nightsky_randomitv",&itv,0))
		cfg_set_uvalue("nightsky_randomitv",0);
	if (cfg_get_uvalue("nightsky_interval",&Interval,40))
		cfg_set_uvalue("nightsky_interval",40);
#endif
	action_add("nightsky!on",nightsky_on,0,"turn on nightsky");
	action_add("nightsky!off",nightsky_off,0,"turn off nightsky");
	action_add("nightsky!toggle",nightsky_toggle,0,"toggle on/off nightsky");
	action_add("nightsky!max",nightsky_fade,(void*)(MAX_BRIGHT),"all stars maximum brightness");
	action_add("nightsky!min",nightsky_fade,0,"all stars dark");
	action_add("nightsky!random",nightsky_random,0,"randomize stars' brightness");
	int e = cyclic_add_task("nightsky_step",nightsky_step);
	if (e != 0)
		return e;
	e = cyclic_add_task("nightsky_update",nightsky_update);
	if (e == 0)
		log_info(TAG,"setup done");
	return e;
}
#endif



/*
int tlc5947(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("tlc5947 {on|off|max|random|list}\nns interval <ms>\nns <led> <value>\nns <value>\n");
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
*/


static void tlc5947_onoff(void *arg)
{
	const char *v = (const char *)arg;
	log_dbug(TAG,"onoff(%s)",v);
	if ((0 == strcmp(v,"on")) || (0 == strcmp(v,"1")))
		Drv->on();
	else if ((0 == strcmp(v,"off")) || (0 == strcmp(v,"0")))
		Drv->off();
}


static void tlc5947_set(void *arg)
{
	char *a = (char *) arg;
	if (a == 0) {
		log_dbug(TAG,"set: missing argument");
		return;
	}
	log_dbug(TAG,"set %s",a);
	char *e;
	unsigned n = Drv->get_nleds();
	unsigned long led = strtoul(a,&e,0);
	if (e == a)
		return;
	if (char *c = strchr(a,',')) {
		++c;
		unsigned long value = strtoul(c,&e,0);
		if (c == e)
			return;
		Drv->set_led(led,value);
	} else {
		for (unsigned i = 0; i < n; ++i)
			Drv->set_led(i,led);
	}
	Drv->commit();
}


static void tlc5947_write(void *arg)
{
	char *a = (char *) arg;
	if (a == 0) {
		log_dbug(TAG,"set: missing argument");
		return;
	}
	log_dbug(TAG,"write %s",a);
	char *e = a;
	int led = 0;
	do {
		unsigned long value = strtoul(a,&e,0);
		if (e != a)
			Drv->set_led(led,value);
		++led;
		a = e + 1;
	} while (*e == ',');
	Drv->commit();
}


#ifdef CONFIG_LUA
int luax_tlc5947_get(lua_State *L)
{
	if (Drv == 0)
		return 0;
	int idx = luaL_checkinteger(L,1);
	uint32_t v = Drv->get_led(idx);
	lua_seti(L,1,v);
	return 1;
}


int luax_tlc5947_set(lua_State *L)
{
	if (Drv == 0)
		return 0;
	int idx = luaL_checkinteger(L,1);
	int val = luaL_checkinteger(L,2);
	Drv->set_led(idx,val);
	Drv->commit();
	return 0;
}


int luax_tlc5947_write(lua_State *L)
{
	if (Drv == 0)
		return 0;
	luaL_checktype(L,1,LUA_TTABLE);
	size_t n = lua_rawlen(L,1);
	for (int x = 0; x < n; ++x) {
		lua_rawgeti(L,1,x);
		long l = lua_tonumber(L,-1);
		Drv->set_led(x,l);
		lua_pop(L,1);
	}
	Drv->commit();
	return 0;
}


static const LuaFn Functions[] = {
	{ "tlc5947_get", luax_tlc5947_get, "TLC5947: get PWM value (ch)" },
	{ "tlc5947_set", luax_tlc5947_set, "TLC5947: set PWM value (ch,v)" },
	{ "tlc5947_write", luax_tlc5947_write, "TLC5947: set PWM values (v,...)" },
	{ 0, 0, 0 },
};
#endif // CONFIG_LUA


void tlc5947_setup()
{
	const Tlc5947Config &c = HWConf.tlc5947();
	if (!c.has_sin() || !c.has_sclk() || !c.has_xlat() || !c.has_blank() || (0 == c.ntlc())) {
		log_dbug(TAG,"not configured");
		return;
	}
	Drv = new TLC5947;
	int r = Drv->init
		( (gpio_num_t)c.sin()
		, (gpio_num_t)c.sclk()
		, (gpio_num_t)c.xlat()
		, (gpio_num_t)c.blank()
		, c.ntlc());
	if (r) {
		delete Drv;
		Drv = 0;
		return;
	}
	Drv->off();
	Drv->commit();
	action_add("tlc5947!onoff",tlc5947_onoff,0,"turn on nightsky");
	action_add("tlc5947!set",tlc5947_set,0,"set <channel>,<value>");
	action_add("tlc5947!write",tlc5947_write,0,"write <value>,...");
#ifdef CONFIG_LUA
	xlua_add_funcs("tlc5947",Functions);
#endif
}
#endif
