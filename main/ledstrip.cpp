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

#include "actions.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "ws2812b.h"

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

/*
#define BLACK	0X000000
#define WHITE	0xffffff
#define RED	0xff0000
#define GREEN	0x00ff00
#define BLUE	0x0000ff
#define MAGENTA	0xff00ff
#define YELLOW	0xffff00
#define CYAN	0x00ffff
*/

#define BLACK	0X000000
#define WHITE	0x202020
#define RED	0x200000
#define GREEN	0x002000
#define BLUE	0x000020
#define MAGENTA	0x200020
#define YELLOW	0x202000
#define CYAN	0x002020
#define PURPLE	0x100010

#define TAG MODULE_RGBLEDS

struct RgbName
{
	uint32_t value;
	const char *name;
};


static const RgbName RgbNames[] = {
	{ BLACK, "black" },
	{ WHITE, "white" },
	{ RED, "red" },
	{ BLUE, "blue" },
	{ MAGENTA, "magenta" },
	{ YELLOW, "yellow" },
	{ CYAN, "cyan" },
	{ PURPLE, "purple" },
	{ GREEN, "green" },
};


static int rgbname_value(const char *n, unsigned long *v)
{
	for (const auto &r : RgbNames) {
		if (0 == strcmp(n,r.name)) {
			*v = r.value;
			return 0;
		}
	}
	char *e;
	long l = strtol(n,&e,0);
	if ((e != n) && ((l & 0xff000000) == 0)) {
		*v = l;
		return 0;
	}
	log_warn(TAG,"invalid color '%s'",n);
	return 1;
}


static void ledstrip_action_set(void *arg)
{
	char *arg0 = (char *) arg;
	if (arg == 0) {
		log_dbug(TAG,"set: missing argument");
		return;
	}
	log_dbug(TAG,"set %s",arg0);
	const char *arg1 = 0, *arg2 = 0;
	if (char *c1 = strchr(arg0,',')) {
		*c1 = 0;
		arg1 = c1+1;
		if (char *c2 = strchr(arg1,',')) {
			*c2 = 0;
			arg2 = c2+1;
		}
	}
	const char *idx = 0, *val = 0;
	WS2812BDrv *drv = WS2812BDrv::get_bus(arg0);
	if (drv == 0) {
		drv = WS2812BDrv::first();
		if (drv == 0)
			return;
		if (arg2)
			return;
		if (arg1) {
			idx = arg0;
			val = arg1;
		} else {
			val = arg0;
		}
	} else if (arg2) {
		idx = arg1;
		val = arg2;
	} else {
		val = arg1;
	}
	char *e;
	int led;

	if (idx) {
		led = strtoul(idx,&e,0);
		if (e == idx)
			led = -1;
	} else {
		led = -1;
	}
	assert(val);
	unsigned long value;
	if (rgbname_value(val,&value))
		return;
	if (led == -1) {
		drv->set_leds(value);
	} else {
		drv->set_led(led,value);
	}
	drv->update();
}


static void ledstrip_action_write(void *arg)
{
	log_dbug(TAG,"action_write");
	char *a = (char *) arg;
	if (a == 0) {
		log_dbug(TAG,"set: missing argument");
		return;
	}
	WS2812BDrv *drv;
	if (char *c = strchr(a,',')) {
		*c = 0;
		drv = WS2812BDrv::get_bus(a);
		if (drv) {
			a = c+1;
		} else {
			*c = ',';
			drv = WS2812BDrv::first();
		}
	} else {
		drv = WS2812BDrv::first();
	}
	if (drv == 0)
		return;
	char *e = a;
	int led = 0;
	do {
		unsigned long value;
		if (rgbname_value(a,&value))
			break;
		drv->set_led(led,value);
		++led;
		a = e + 1;
	} while (*e == ',');
	drv->update();
}


#ifdef CONFIG_LUA
int luax_rgbleds_get(lua_State *L)
{
	WS2812BDrv *drv;
	if (lua_isstring(L,1)) {
		drv = WS2812BDrv::get_bus(lua_tostring(L,1));
	} else {
		drv = WS2812BDrv::first();
	}
	if (0 == drv) {
		lua_pushliteral(L,"Invalid bus.");
		lua_error(L);
	}
	int idx = luaL_checkinteger(L,1);
	uint32_t v = drv->get_led(idx);
	lua_seti(L,1,v);
	return 1;
}


int luax_rgbleds_set(lua_State *L)
{
	// (val) : one value for all leds on primary bus
	// (idx,val) : set led at idx to value on primary bus
	// (bus,val)
	// (bus,idx,val)
	WS2812BDrv *drv = 0;
	unsigned arg = 1;
	if (lua_isstring(L,arg)) {
		drv = WS2812BDrv::get_bus(lua_tostring(L,1));
		if (drv) {
			++arg;
		} else if (lua_isstring(L,2)) {
			lua_pushliteral(L,"Invalid bus.");
			lua_error(L);
		}
	}
	if (drv == 0) {
		drv = WS2812BDrv::first();
		if (0 == drv) {
			lua_pushliteral(L,"No bus.");
			lua_error(L);
		}
	}
	unsigned idx = 0;
	if (lua_isinteger(L,arg)) {
		idx = lua_tointeger(L,arg);
	} else if (lua_isstring(L,arg)) {
		unsigned long v;
		if (rgbname_value(lua_tostring(L,arg),&v)) {
			lua_pushliteral(L,"Invalid argument.");
			lua_error(L);
		}
		drv->set_leds(v);
		drv->update();
		return 0;
	} else {
		lua_pushliteral(L,"Invalid argument.");
		lua_error(L);
	}
	++arg;
	if (lua_isinteger(L,arg)) {
		int val = lua_tointeger(L,arg);
		drv->set_led(idx,val);
	} else {
		drv->set_leds(idx);
	}
	drv->update();
	return 0;
}


int luax_rgbleds_write(lua_State *L)
{
	WS2812BDrv *drv;
	unsigned arg;
	if (lua_isstring(L,1)) {
		drv = WS2812BDrv::get_bus(lua_tostring(L,1));
		arg = 2;
	} else {
		drv = WS2812BDrv::first();
		arg = 1;
	}
	if (0 == drv) {
		lua_pushliteral(L,"Invalid bus.");
		lua_error(L);
	}
	luaL_checktype(L,arg,LUA_TTABLE);
	size_t n = lua_rawlen(L,arg);
	for (int x = 0; x < n; ++x) {
		lua_rawgeti(L,arg,x);
		long l = lua_tonumber(L,-1);
		drv->set_led(x,l);
		lua_pop(L,arg);
	}
	drv->update();
	return 0;
}


int luax_rgbleds_num(lua_State *L)
{
	WS2812BDrv *drv;
	if (lua_isstring(L,1)) {
		drv = WS2812BDrv::get_bus(lua_tostring(L,1));
	} else {
		drv = WS2812BDrv::first();
	}
	if (0 == drv) {
		lua_pushliteral(L,"Invalid bus.");
		lua_error(L);
	}
	lua_pushinteger(L,drv->num_leds());
	return 1;
}


static LuaFn Functions[] = {
	{ "rgbleds_get", luax_rgbleds_get, "WS2812b: get LED value ([bus,]idx)" },
	{ "rgbleds_set", luax_rgbleds_set, "WS2812b: set LEDs value ([[bus,]idx,]val)" },
	{ "rgbleds_write", luax_rgbleds_write, "WS2812b: write LED values ([bus,]v,...)"  },
	{ "rgbleds_num", luax_rgbleds_num, "WS2812b: return number of LEDs ([bus])"  },
	{ 0, 0, 0 }
};
#endif


void rgbleds_setup()
{
	unsigned num = 0;
	for (const auto &c : HWConf.ws2812b()) {
#ifdef CONFIG_IDF_TARGET_ESP8266
		if ((!c.has_gpio() || (0 == c.nleds())))
#else
		if ((!c.has_gpio() || (0 == c.nleds())) || !c.has_ch())
#endif
		{
			log_dbug(TAG,"incomplete config");
			continue;
		}
		size_t nl = c.name().size();
		char name[nl < 16 ? 16 : nl + 1];
		strcpy(name,c.name().c_str());
		gpio_num_t gpio = (gpio_num_t) c.gpio();
		if (name[0] == 0)
			sprintf(name,"ws2812b@%u",gpio);
		log_dbug(TAG,"setup %s",name);
		unsigned nleds = c.nleds();
		WS2812BDrv *drv = new WS2812BDrv(name);
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
		if (drv->init((gpio_num_t)c.gpio(),nleds,(rmt_channel_t)c.ch()))
			continue;
#else
		if (drv->init((gpio_num_t)c.gpio(),nleds))
			continue;
#endif
		++num;
		drv->set_leds(0);
	}
	if (num) {
		action_add("rgbleds!set",ledstrip_action_set,0,"set color of led(s) on strip");
		action_add("rgbleds!write",ledstrip_action_write,0,"write multiple values to LEDs");
#ifdef CONFIG_LUA
		xlua_add_funcs("rgbleds",Functions);
#endif
	}
}

#endif
