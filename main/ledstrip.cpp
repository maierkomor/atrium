/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#define TAG MODULE_LEDSTRIP

struct RgbName
{
	uint32_t value;
	const char *name;
};


static WS2812BDrv *LED_Strip = 0;

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


#if 0 // demo mode
static uint32_t ColorMap[] = {
	BLACK, WHITE, RED, GREEN, BLUE, MAGENTA, YELLOW, CYAN, PURPLE
};

static void ledstrip_task(void *arg)
{
	uint8_t numleds = (uint8_t) (unsigned) arg;
	uint8_t off = 0;
	LED_Strip->reset();
	log_dbug(TAG,"0");
	vTaskDelay(1000/portTICK_PERIOD_MS);
	LED_Strip->set_leds(WHITE);
	log_dbug(TAG,"1");
	LED_Strip->update();
	vTaskDelay(1000/portTICK_PERIOD_MS);
	LED_Strip->set_leds(BLACK);
	log_dbug(TAG,"2");
	LED_Strip->update();
	vTaskDelay(1000/portTICK_PERIOD_MS);
	for (unsigned x = 0; x < 256; ++x) {
		log_dbug(TAG,"3");
		LED_Strip->set_leds(x << 16 | x << 8 | x);
		LED_Strip->update();
		vTaskDelay(50/portTICK_PERIOD_MS);
	}
	for (;;) {
		for (int i = 0; i < numleds; ++i)
			LED_Strip->set_led(i,ColorMap[(i+off)%(sizeof(ColorMap)/sizeof(ColorMap[0]))]);
		log_dbug(TAG,"4");
		LED_Strip->update();
		if (++off == sizeof(ColorMap)/sizeof(ColorMap[0]))
			off = 0;
		vTaskDelay(1000/portTICK_PERIOD_MS);

		for (int i = 0; i < numleds; ++i)
			LED_Strip->set_led(i,1<<i);
		LED_Strip->update();
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}

#elif 0

static void execute_op(ledaction_t a, uint32_t arg)
{
	switch (a) {
	case la_nop:
		break;
	case la_disp:
		// TODO
		break;
	case la_set:
		LED_Strip->set_leds(arg>>24,arg&0xffffff);
		break;
	case la_setall:
		LED_Strip->set_leds(arg);
		break;
	case la_delay:
		vTaskDelay(arg);
		break;
		/*
	case la_setrd:
		LED_Strip->set_leds((arg<<16)&0xff0000);
		break;
	case la_setgr:
		LED_Strip->set_leds((arg<<8)&0xff00);
		break;
	case la_setbl:
		LED_Strip->set_leds((arg)&0xff);
		break;
		*/
	case la_update:
		LED_Strip->update();
		break;
	case la_fade:
		LED_Strip->update(true);
	case la_mode:
		break;
	case la_jump:
		break;
	case la_mode:
		break;
	default:
		log_warn(TAG,"unknown ledstrip command %d",a);
	}
}

static void ledstrip_task(void *arg)
{
	for (;;) {
		uint32_t r = esp_random();
		LED_Strip->set_leds(r&0xff0000);
		vTaskDelay((((r&0x1f)<<3)+20)/portTICK_PERIOD_MS);
		LED_Strip->update();
	}
}
#endif



static int rgbname_value(const char *n, unsigned long *v)
{
	for (const auto &r : RgbNames) {
		if (0 == strcmp(n,r.name)) {
			*v = r.value;
			return 0;
		}
	}
	log_warn(TAG,"unknown color name %s",n);
	return 1;
}


static void ledstrip_action_set(void *arg)
{
	char *a = (char *) arg;
	if (a == 0) {
		log_dbug(TAG,"set: missing argument");
		return;
	}
	log_dbug(TAG,"set %s",a);
	char *e;
	unsigned long led = strtoul(a,&e,0);
	if (e == a) {
		unsigned long value;
		if (rgbname_value(a,&value))
			return;
		LED_Strip->set_leds(value);
	} else if (char *c = strchr(a,',')) {
		unsigned long value = strtoul(c+1,&e,0);
		if ((c+1 == e) && rgbname_value(c+1,&value))
			return;
		LED_Strip->set_led(led,value);
	} else {
		LED_Strip->set_leds(led);
	}
	LED_Strip->update();
}


static void ledstrip_action_write(void *arg)
{
	log_dbug(TAG,"action_write");
	char *a = (char *) arg;
	if (a == 0) {
		log_dbug(TAG,"set: missing argument");
		return;
	}
	char *e = a;
	int led = 0;
	do {
		unsigned long value = strtoul(a,&e,0);
		if (e != a)
			LED_Strip->set_led(led,value);
		++led;
		a = e + 1;
	} while (*e == ',');
	LED_Strip->update();
}


#ifdef CONFIG_LUA
int luax_rgbleds_get(lua_State *L)
{
	if (LED_Strip == 0) {
		log_warn(TAG,"Lua: no strip");
		return 0;
	}
	int idx = luaL_checkinteger(L,1);
	uint32_t v = LED_Strip->get_led(idx);
	lua_seti(L,1,v);
	return 1;
}


int luax_rgbleds_set(lua_State *L)
{
	if (LED_Strip == 0) {
		log_warn(TAG,"Lua: no strip");
		return 0;
	}
	int idx = luaL_checkinteger(L,1);
	if (lua_isinteger(L,2)) {
		int val = lua_tointeger(L,2);
		LED_Strip->set_led(idx,val);
	} else {
		LED_Strip->set_leds(idx);
	}
	LED_Strip->update();
	return 0;
}


int luax_rgbleds_write(lua_State *L)
{
	if (LED_Strip == 0) {
		log_warn(TAG,"Lua: no strip");
		return 0;
	}
	luaL_checktype(L,1,LUA_TTABLE);
	size_t n = lua_rawlen(L,1);
	for (int x = 0; x < n; ++x) {
		lua_rawgeti(L,1,x);
		long l = lua_tonumber(L,-1);
		LED_Strip->set_led(x,l);
		lua_pop(L,1);
	}
	LED_Strip->update();
	return 0;
}

static LuaFn Functions[] = {
	{ "rgbleds_get", luax_rgbleds_get, "WS2812b: get LED value" },
	{ "rgbleds_set", luax_rgbleds_set, "WS2812b: set LED value (i,v) or set LEDs value (v)" },
	{ "rgbleds_write", luax_rgbleds_write, "WS2812b: write LED values (v,...)"  },
	{ 0, 0, 0 }
};
#endif


void rgbleds_setup()
{
	if (!HWConf.has_ws2812b())
		return;
	const Ws2812bConfig &c = HWConf.ws2812b();
	if ((!c.has_gpio() || (0 == c.nleds()))) {
		log_dbug(TAG,"incomplete config");
		return;
	}
	log_dbug(TAG,"setup");
	unsigned nleds = c.nleds();
	LED_Strip = new WS2812BDrv;
	action_add("rgbleds!set",ledstrip_action_set,0,"set color of led(s) on strip");
	action_add("rgbleds!write",ledstrip_action_write,0,"write multiple values to LEDs");
#ifdef CONFIG_IDF_TARGET_ESP32
	if (LED_Strip->init((gpio_num_t)c.gpio(),nleds),(rmt_channel_t)c.ch())
		return;
#else
	if (LED_Strip->init((gpio_num_t)c.gpio(),nleds))
		return;
#endif
	LED_Strip->set_leds(0);
#ifdef CONFIG_LUA
	xlua_add_funcs("rgbleds",Functions);
#endif
}

#endif
