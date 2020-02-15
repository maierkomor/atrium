/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#ifdef CONFIG_MQTT
#include "binformats.h"
#include "cyclic.h"
#include "globals.h"
#include "log.h"
#include "mqtt.h"
#include "settings.h"
#include "support.h"
#include "terminal.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <mqtt_client.h>
#include <string.h>
#include <time.h>

#include <map>

using namespace std;


static char TAG[] = "mqtt";
static bool Running, Connected;
static esp_mqtt_client_handle_t Client = 0;
static multimap<string,void(*)(const char *,size_t)> Subscriptions;
static string DMesg;
static SemaphoreHandle_t Mtx = 0;


static void execute_callbacks(const char *t, size_t tl, const char *d, size_t dl)
{
	string topic(t,tl);
	auto r = Subscriptions.equal_range(topic);
	log_info(TAG,"callbacks for %s",topic.c_str());
	while (r.first != r.second) {
		log_info(TAG,"execute callback %p",r.first->second);
		r.first->second(d,dl);
		++r.first;
	}
}


static void subscribe_topics()
{
	for (auto i = Subscriptions.begin(), e = Subscriptions.end(); i != e; ++i) {
		const char *t = i->first.c_str();
		int id = esp_mqtt_client_subscribe(Client,t,0);
		if (id)
			log_info(TAG,"subscribed to %s",t);
		else
			log_warn(TAG,"failed to subscribe %s",t);
	}

}


static int mqtt_event_handler(esp_mqtt_event_handle_t e)
{
	switch (e->event_id) {
	case MQTT_EVENT_CONNECTED:
		log_info(TAG,"connected");
		Connected = true;
		mqtt_publish("version",Version,strlen(Version),1);
		subscribe_topics();
		break;
	case MQTT_EVENT_DISCONNECTED:
		log_info(TAG,"disconnected");
		Connected = false;
		break;
	case MQTT_EVENT_SUBSCRIBED:
		log_info(TAG, "subscribed msg_id=%d", e->msg_id);
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		log_info(TAG, "unsubscribed msg_id=%d", e->msg_id);
		break;
	case MQTT_EVENT_PUBLISHED:
		log_info(TAG, "pusblished msg_id=%d", e->msg_id);
		break;
	case MQTT_EVENT_DATA:
		log_info(TAG, "data: topic='%.*s', data='%.*s'",e->topic_len,e->topic,e->data_len,e->data);
		execute_callbacks(e->topic,e->topic_len,e->data,e->data_len);
		break;
	case MQTT_EVENT_ERROR:
		log_info(TAG, "MQTT_EVENT_ERROR");
		break;
	default:
		log_info(TAG, "unknown event %d", e->event_id);
		break;
	}
	return 0;
}


void mqtt_start(void)
{
	if (Running) {
		log_warn(TAG,"mqtt already running");
		return;
	}
	if (!Config.has_mqtt()) {
		log_warn(TAG,"mqtt not configured");
		return;
	}
	if (!Config.mqtt().enable()) {
		log_warn(TAG,"mqtt disabled");
		return;
	}
	if (!Config.mqtt().has_uri()) {
		log_warn(TAG,"mqtt disabled");
		return;
	}
	if (!wifi_station_isup()) {
		log_warn(TAG,"wifi station is not up");
		return;
	}
	esp_mqtt_client_config_t cfg;
	memset(&cfg,0,sizeof(cfg));
	const MQTT &m = Config.mqtt();
	if (m.has_username())
		cfg.username = m.username().c_str();
	if (m.has_password())
		cfg.password = m.password().c_str();
	cfg.uri = m.uri().c_str();
	cfg.event_handle = mqtt_event_handler;
	if (Config.has_nodename())
		cfg.client_id = Config.nodename().c_str();
	Client = esp_mqtt_client_init(&cfg);
	if (esp_err_t e = esp_mqtt_client_start(Client)) {
		log_error(TAG,"mqtt start failed: %x\n",e);
		Running = false;
	} else {
		Running = true;
	}
}


void mqtt_stop(void)
{
	if (!Running)
		log_warn(TAG,"mqtt not running");
	if (esp_err_t e = esp_mqtt_client_stop(Client))
		log_error(TAG,"stop failed %x",e);
	else
		log_info(TAG,"stopped");
	Running = false;
}


int mqtt_publish(const char *t, const char *v, int len, int retain)
{
	if (!Connected)
		return ENOTCONN;
	char topic[128];
	int n = snprintf(topic,sizeof(topic),"%s/%s",Config.nodename().c_str(),t);
	if (n >= sizeof(topic))
		return EINVAL;
	xSemaphoreTake(Mtx,portMAX_DELAY);
	int e = (int) esp_mqtt_client_publish(Client,topic,v,len,0,retain);
	xSemaphoreGive(Mtx);
	return e;
}


void mqtt_set_dmesg(const char *m, size_t s)
{
	if (0 == Mtx)
		Mtx = xSemaphoreCreateMutex();
	xSemaphoreTake(Mtx,portMAX_DELAY);
	if (s > 120)
		s = 120;
	DMesg.assign(m,s);
	xSemaphoreGive(Mtx);
}


void mqtt_subscribe(const char *topic, void (*callback)(const char *,size_t))
{
	size_t ns = Config.nodename().size();
	size_t tl = strlen(topic);
	char t[tl+2+ns];
	memcpy(t,Config.nodename().data(),ns);
	t[ns] = '/';
	memcpy(t+ns+1,topic,tl+1);
	log_info(TAG,"add callback %s",topic);
	if (Client != 0) {
		int id = esp_mqtt_client_subscribe(Client,t,0);
		if (id)
			log_info(TAG,"subscribed to %s, id %d",topic,id);
		else
			log_warn(TAG,"subscribe %s failed",topic);
	}
	Subscriptions.insert(pair<string,void(*)(const char*,size_t)>(string(t),callback));
}


static unsigned mqtt_cyclic()
{
	static unsigned ltime = 0;
	if (!Running) {
		if (Config.has_mqtt() && Config.mqtt().enable() && wifi_station_isup())
			mqtt_start();
		return 1000;
	}
	if (!Connected)
		return 1000;
#ifdef _POSIX_MONOTONIC_CLOCK
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	uint32_t uptime = ts.tv_sec;
#else
	uint64_t uptime = clock() / CLOCKS_PER_SEC;
#endif
	unsigned d = uptime/(60*60*24);
	uptime -= d*(60*60*24);
	unsigned h = uptime/(60*60);
	uptime -= h*60*60;
	unsigned m = uptime/60;
	unsigned t = m + h * 60 + d * 60 * 24;
	int n = 0;
	char value[32];
	if (t != ltime) {
		ltime = t;
		if (d > 0)
			n = snprintf(value,sizeof(value),"%u days, %d:%02u",d,h,m);
		else
			n = snprintf(value,sizeof(value),"%d:%02u",h,m);
	}
	if (0 == Mtx)
		xSemaphoreTake(Mtx,portMAX_DELAY);
	if ((n > 0) && (n <= sizeof(value)))
		mqtt_publish("uptime",value,n,0);
	if (!DMesg.empty()) {
		mqtt_publish("dmesg",DMesg.data(),DMesg.size(),0);
		DMesg.clear();
	}
	xSemaphoreGive(Mtx);
	return 1000;
}


int mqtt(Terminal &term, int argc, const char *args[])
{
	if (argc > 3) {
		term.printf("%s: 1-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if ((argc == 1) || (!strcmp(args[1],"status"))) {
		if (Config.has_mqtt()) {
			const MQTT &mqtt = Config.mqtt();
			term.printf("URI: %s\n",mqtt.uri().c_str());
			term.printf("user: %s\n",mqtt.username().c_str());
			term.printf("pass: %s\n",mqtt.password().c_str());
			term.printf("MQTT %sabled\n",mqtt.enable() ? "en" : "dis");
			term.printf("MQTT is %sconnected\n",Connected ? "" : "not ");
		} else {
			term.printf("MQTT is not configured\n");
		}
		return 0;
	}
	if (!strcmp(args[1],"uri")) {
		if (argc == 3)
			Config.mutable_mqtt()->set_uri(args[2]);
		else
			term.printf("invalid argument\n");
	} else if (!strcmp(args[1],"user")) {
		if (argc == 3)
			Config.mutable_mqtt()->set_username(args[2]);
		else if (argc == 2)
			Config.mutable_mqtt()->clear_username();
		else
			term.printf("invalid argument\n");
	} else if (!strcmp(args[1],"pass")) {
		if (argc == 3)
			Config.mutable_mqtt()->set_password(args[2]);
		else if (argc == 2)
			Config.mutable_mqtt()->clear_password();
		else
			term.printf("invalid argument\n");
	} else if (!strcmp(args[1],"enable")) {
		if (!Config.mqtt().enable()) {
			mqtt_start();
			Config.mutable_mqtt()->set_enable(true);
		}
	} else if (!strcmp(args[1],"disable")) {
		if (Config.mqtt().enable()) {
			mqtt_stop();
			Config.mutable_mqtt()->set_enable(false);
		}
	} else if (!strcmp(args[1],"clear")) {
		Config.mutable_mqtt()->clear();
	} else if (!strcmp(args[1],"start")) {
		mqtt_start();
	} else if (!strcmp(args[1],"stop")) {
		mqtt_stop();
	} else if (!strcmp(args[1],"-h"))
		term.printf("valid arguments: status, uri, user, pass, enable, disable, clear\n");
	else
		return 1;
	return 0;
}


void mqtt_setup(void)
{
	Running = false;
	Connected = false;
	if (0 == Mtx)
		Mtx = xSemaphoreCreateMutex();
	add_cyclic_task("mqtt",mqtt_cyclic);
}

#endif // CONFIG_MQTT
