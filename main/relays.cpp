/*
 *  Copyright (C) 2020-2022, Thomas Maier-Komor
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

#ifdef CONFIG_RELAY

#include "actions.h"
#include "env.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "nvm.h"
#include "mqtt.h"
#include "profiling.h"
#include "relay.h"
#include "settings.h"
#include "terminal.h"

#include <string.h>

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

using namespace std;

#define TAG MODULE_RELAY


#ifdef CONFIG_MQTT
static void relay_callback(Relay *r)
{
	bool on = r->is_on();
	mqtt_pub_nl(r->name(),on?"on":"off",on?2:3,1,1);
}
#endif


static int relay_set(const char *n, bool on)
{
	if (Relay *r = Relay::get(n)) {
		r->set(on);
		return 0;
	}
	log_warn(TAG,"set(%s): unknown relay",n);
	return 1;
}


static void relay_set_state(void *arg)
{
	const char *a = (const char *) arg;
	char *sp = strchr(a,':');
	if (sp == 0) {
		sp = strchr(a,'=');
		if (sp == 0) {
			sp = strchr(a,' ');
			if (sp == 0) {
				log_warn(TAG,"relay!set invalid arg '%s'",a);
				return;
			}
		}
	}
	*sp = 0;
	if (Relay *r = Relay::get(a)) {
		++sp;
		log_dbug(TAG,"mqtt: %s: %s",a,sp);
		if (0 == strcmp(sp,"toggle"))
			r->toggle();
		else if (0 == strcmp(sp,"on"))
			r->turn_on();
		else if (0 == strcmp(sp,"off"))
			r->turn_off();
		else if (0 == strcmp(sp,"1"))
			r->turn_on();
		else if (0 == strcmp(sp,"0"))
			r->turn_off();
		else
			log_warn(TAG,"invalid mqtt request: %s: %s",a,sp);
	}
}


#ifdef CONFIG_MQTT
static void mqtt_callback(const char *topic, const void *data, size_t len)
{
	const char *sl = strchr(topic,'/');
	if ((sl == 0) || (0 != memcmp(sl+1,"set_",4))) {
		log_warn(TAG,"unknown topic %s",topic);
		return;
	}
	log_info(TAG,"mqtt_cb: %s %.*s",sl+5,len,(const char *)data);
	const char *text = (const char *)data;
	if (len == 1) {
		if ('0' == text[0])
			relay_set(sl+5,false);
		else if ('1' == text[0])
			relay_set(sl+5,true);
	} else if (len == 2) {
		if (0 == memcmp("on",data,2))
			relay_set(sl+5,true);
		else if (0 == memcmp("-1",data,2))
			relay_set(sl+5,false);
	} else if ((len == 3) && (0 == memcmp("off",data,3))) {
		relay_set(sl+5,false);
	} else if ((len == 6) && (0 == memcmp("toggle",data,6))) {
		if (Relay *r = Relay::get(sl+5)) 
			r->set(r->is_on()^1);
		else
			log_warn(TAG,"mqtt: unknown relay %s",sl+5);
	} else
		log_warn(TAG,"MQTT arg: %.s",len,data);
}
#endif


#ifdef CONFIG_LUA
static int luax_relay_set(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	int v = luaL_checkinteger(L,2);
	if ((v < 0) || (v > 1)) {
		lua_pushliteral(L,"Invalid argument #2.");
		lua_error(L);
	}
	if (Relay *r = Relay::get(n)) {
		r->set(v);
	} else {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	return 0;
}


static int luax_relay_toggle(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	if (Relay *r = Relay::get(n)) {
		r->toggle();
	} else {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	return 0;
}


static LuaFn Functions[] = {
	{ "relay_set", luax_relay_set, "turn relay on/off" },
	{ "relay_toggle", luax_relay_toggle, "toggle relay state" },
	{ 0, 0, 0 }
};
#endif


void relay_setup()
{
	if (Relay::first()) {
		log_warn(TAG,"duplicate init");
		return;
	}
	unsigned numrel = 0;
	for (auto &c : *HWConf.mutable_relay()) {
		int8_t gpio = c.gpio();
		if (gpio < 0)
			continue;
		++numrel;
		unsigned itv = c.min_itv();
		const char *n = c.name().c_str();
		if (*n == 0) {
			char name[12];
			sprintf(name,"relay@%d",(int)gpio);
			c.set_name(name);
			n = c.name().c_str();
		}
#ifdef CONFIG_MQTT
		if (c.config_mqtt()) {
			char topic[c.name().size()+5] = "set_";
			strcpy(topic+4,n);
			log_dbug(TAG,"subscribe %s",topic);
			mqtt_sub(topic,mqtt_callback);
		}
#endif
		
		if (Relay *r = Relay::create(n,(xio_t)gpio,itv,c.config_active_high())) {
#ifdef CONFIG_MQTT
			r->setCallback(relay_callback);
#endif
			bool iv = c.config_init_on();
			if (c.config_persistent()) {
				r->setPersistent(true);
				iv = nvm_read_u8(n,iv);
			}
			r->attach(RTData);
			log_info(TAG,"relay '%s' at gpio%d init %d",n,gpio,(int)iv);
			r->set(iv);
		} else {
			log_warn(TAG,"%s at %u: error",n,gpio);
		}
	}
	// update interlocks
	for (const auto &c : HWConf.relay()) {
		if (!c.has_gpio() || !c.has_name())
			continue;
		int il = c.interlock();
		if ((il != -1) && (il < HWConf.relay_size())) {
			const char *n = HWConf.relay(il).name().c_str();
			Relay *ir = Relay::get(n);
			assert(ir);
			Relay *r = Relay::get(c.name().c_str());
			assert(r);
			r->setInterlock(r);
		}
	}
	if (numrel) {
		action_add("relay!set",relay_set_state,0,"set relay state: '<name>:{on,off,toggle}'");
#ifdef CONFIG_LUA
		xlua_add_funcs("relay",Functions);
#endif
	}
}


const char *relay(Terminal &term, int argc, const char *args[])
{
	Relay *r = Relay::first();
	if (r == 0) {
		term.println("no relays");
	} else if (argc == 1) {
		while (r) {
			term.printf("relay '%s' at gpio%d is %s\n",r->name(),r->gpio(),r->is_on() ? "on" : "off");
			if (Relay *il = r->getInterlock())
				term.printf("\tinterlocked with %s (%s)\n",il->name(),il->is_on() ? "locked" : "free");
			r = r->next();
		}
		return 0;
	} else if (argc == 3) {
		bool v;
		if (0 == strcmp("on",args[2]))
			v = true;
		else if (0 == strcmp("off",args[2]))
			v = false;
		else
			return "Invalid argument #1.";
		return relay_set(args[1],v) ? "Failed." : 0;
	}
	return "Invalid number of arguments.";
}

#endif	// CONFIG_RELAY
