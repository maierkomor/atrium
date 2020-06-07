/*
 *  Copyright (C) 2017-2020, Thomas Maier-Komor
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

#define CONFIG_RELAY_OFF (1^CONFIG_RELAY_ON)

#include "actions.h"
#include "cyclic.h"
#include "binformats.h"
#include "globals.h"
#include "influx.h"
#include "log.h"
#include "mqtt.h"
#include "relay.h"
#include "settings.h"

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#if defined ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif

#if CONFIG_RELAY_GPIO < 0 || CONFIG_RELAY_GPIO >= GPIO_PIN_COUNT
#error gpio value for relay out of range
#endif


static char TAG[] = "relay";

static uint32_t OnTime = 0;


void relay_on()
{
	if (RTData.relay())
		return;
	gpio_set_level((gpio_num_t)CONFIG_RELAY_GPIO,CONFIG_RELAY_ON);
	OnTime = uptime();
	char cstr[8];
	uint8_t h,m;
	get_time_of_day(&h,&m);
	if (h < 24) {
		snprintf(cstr,sizeof(cstr),"%2u:%02u",h,m);
		RTData.set_laston(cstr);
		log_info(TAG,"relay on at %s",cstr);
	} else {
		RTData.clear_laston();
		log_info(TAG,"relay on at %u",OnTime);
	}
	RTData.set_relay(true);
#ifdef CONFIG_MQTT
	mqtt_publish("relay","on",2,1);
	mqtt_publish("last_on",RTData.laston().c_str(),0,1);
#endif
#ifdef CONFIG_INFLUX
	influx_send("relay=1");
#endif
#ifdef CONFIG_RELAY_RESTORE
	store_nvs_u8("relay",1);
#endif
}


void relay_off()
{
	if (!RTData.relay())
		return;
	gpio_set_level((gpio_num_t)CONFIG_RELAY_GPIO,CONFIG_RELAY_OFF);
	char cstr[8];
	uint8_t h,m;
	get_time_of_day(&h,&m);
	if (h < 24) {
		snprintf(cstr,sizeof(cstr),"%2u:%02u",h,m);
		RTData.set_lastoff(cstr);
		log_info(TAG,"relay off at %s\n",RTData.lastoff().c_str());
	} else {
		RTData.clear_lastoff();
		log_info(TAG,"relay off at %u\n",uptime());
	}
	RTData.set_relay(false);
#ifdef CONFIG_MQTT
	mqtt_publish("relay","off",3,1);
	mqtt_publish("last_off",RTData.lastoff().c_str(),0,1);
#endif
#ifdef CONFIG_INFLUX
	influx_send("relay=-1");
#endif
#ifdef CONFIG_RELAY_RESTORE
	store_nvs_u8("relay",0);
#endif
}


void relay_toggle()
{
	if (RTData.relay())
		relay_off();
	else
		relay_on();
}


bool relay_state()
{
	return RTData.relay();
}


unsigned relay_check()
{
	if (Config.has_max_on_time() && RTData.relay() && ((uptime() - OnTime) >= Config.max_on_time() * 60000)) {
		relay_off();
	}
	return 1000;
}


#ifdef CONFIG_MQTT
static void mqtt_callback(const char *d, size_t dl)
{
	if (dl == 1) {
		if (d[0] == '0')
			relay_off();
		else if (d[0] == '1')
			relay_on();
	} else if (dl == 2) {
		if (0 == memcmp(d,"on",2))
			relay_on();
	} else if (dl == 3) {
		if (0 == memcmp(d,"off",3))
			relay_off();
	}
}
#endif


extern "C"
void relay_setup()
{
	log_info(TAG,"setup");
	add_action("relay_on",relay_on,"Strom einschalten");
	add_action("relay_off",relay_off,"Strom ausschalten");
	gpio_pad_select_gpio((gpio_num_t)CONFIG_RELAY_GPIO);
	gpio_set_direction((gpio_num_t)CONFIG_RELAY_GPIO,GPIO_MODE_OUTPUT);
#ifdef CONFIG_RELAY_RESTORE
	if (read_nvs_u8("relay",CONFIG_RELAY_POWERUP_ON ? CONFIG_RELAY_ON : CONFIG_RELAY_OFF)) {
		log_info(TAG,"restoring state to on");
		relay_on();
	} else {
		log_info(TAG,"restoring state to off");
		relay_off();
	}
#elif CONFIG_RELAY_POWERUP_ON == 1
	relay_on();
#else
	relay_off();
#endif
#ifdef CONFIG_MQTT
	mqtt_subscribe("set_relay",mqtt_callback);
#endif
	add_cyclic_task("relay_check",relay_check,0);
}

#endif	// CONFIG_RELAY
