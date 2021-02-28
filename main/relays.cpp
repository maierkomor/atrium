/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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

#include "binformats.h"
#include "cyclic.h"
#include "globals.h"
#include "ujson.h"
#include "log.h"
#include "mqtt.h"
#include "profiling.h"
#include "relay.h"
#include "settings.h"
#include "terminal.h"

#include <string.h>
#include <vector>

using namespace std;

static char TAG[] = "relays";


static void relay_callback(Relay *r)
{
	TimeDelta dt(__FUNCTION__);

	uint16_t c = r->config();
	if (c & rc_persistent)
		store_nvs_u8(r->name(),r->is_on());
	rtd_lock();
	JsonObject *o = static_cast<JsonObject *>(RTData->get(r->name()));
	assert(o);
	JsonInt *i = static_cast<JsonInt*>(o->get("on"));
	assert(i);
	JsonString *s = static_cast<JsonString*>(o->get("state"));
	assert(s);
	bool on = r->is_on();
	JsonString *l = static_cast<JsonString*>(o->get(on ? "laston" : "lastoff"));
	i->set(on ? 1 : -1);
	s->set(on ? "on" : "off");
	l->set(Localtime->get());
	rtd_unlock();
#ifdef CONFIG_MQTT
	mqtt_pub(r->name(),on?"on":"off",on?2:3,1);
#endif
}


static int relay_set(const char *n, bool on)
{
	if (Relay *r = Relay::get(n)) {
		r->set(on);
		return 0;
	}
	log_warn(TAG,"set(%s): unknown relay",n);
	return 1;
}


#ifdef CONFIG_MQTT
static void mqtt_callback(const char *topic, const void *data, size_t len)
{
	const char *sl = strchr(topic,'/');
	if ((sl == 0) || (0 != memcmp(sl+1,"set_",4))) {
		log_warn(TAG,"unknown topic %s",topic);
		return;
	}
	log_info(TAG,"mqtt_cb: set %s",sl+5);
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
		log_warn(TAG,"mqtt arg: %.s",len,data);
}
#endif


int relay_setup()
{
	if (Relay::first()) {
		log_warn(TAG,"already initialized");
		return 0;
	}
	for (const auto &c : HWConf.relay()) {
		if (!c.has_gpio() || !c.has_name())
			continue;
		int8_t gpio = c.gpio();
		if (gpio < 0)
			continue;
		unsigned itv = c.min_itv();
		const char *n = c.name().c_str();
		uint8_t cfg = (uint8_t)c.config();
#ifdef CONFIG_MQTT
		if (cfg & rc_mqtt) {
			char topic[c.name().size()+5] = "set_";
			strcpy(topic+4,n);
			mqtt_sub(topic,mqtt_callback);
			free(topic);
		}
#endif
		Relay *r = new Relay(n,(gpio_num_t)gpio,cfg,itv);
		r->setCallback(relay_callback);
		bool iv;
		if (cfg & rc_persistent) {
			iv = (read_nvs_u8(n,(cfg&rc_init_on) != 0));
		} else {
			iv = ((cfg&rc_init_on) != 0);
		}
		JsonObject *o = RTData->add(n);
		o->add("on",iv?1:-1);
		o->add("state",iv?"on":"off");
		o->add("laston",iv?Localtime->get():"");
		o->add("lastoff",iv?"":Localtime->get());
		log_dbug(TAG,"relay '%s' at gpio%d init %d",n,gpio,(int)iv);
		r->set(iv);
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
	return 0;
}


int relay(Terminal &term, int argc, const char *args[])
{
	Relay *r = Relay::first();
	if (r == 0) {
		term.printf("no relays attached\n");
		return 1;
	}
	if (argc == 1) {
		while (r) {
			term.printf("relay '%s' at gpio%d is %s",r->name(),r->gpio(),r->is_on() ? "on" : "off");
			if (Relay *il = r->getInterlock())
				term.printf(", interlocked with %s (%s)",il->name(),il->is_on() ? "locked" : "free");
			term.println("");
			r = r->next();
		}
		return 0;
	} else if (argc == 3) {
		if (0 == strcmp("on",args[2]))
			return relay_set(args[1],true);
		else if (0 == strcmp("off",args[2]))
			return relay_set(args[1],false);
		else
			return 1;
	}
	return 1;
}

#endif	// CONFIG_RELAY
