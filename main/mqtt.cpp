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

#ifdef CONFIG_MQTT
#include "actions.h"
#include "binformats.h"
#ifdef CONFIG_SIGNAL_PROC
#include "dataflow.h"
#endif
#include "func.h"
#include "globals.h"
#include "log.h"
#include "mqtt.h"
#include "mstream.h"
#include "settings.h"
#include "shell.h"
#include "support.h"
#include "terminal.h"
#include "ujson.h"
#include "wifi.h"
#include "versions.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/apps/mqtt.h>

#include <string.h>
#include <time.h>

#include <map>

using namespace std;


static mqtt_client_t *Client = 0;
static const char TAG[] = "mqtt";
static multimap<estring,void(*)(const char *,const void*,size_t)> Subscriptions;
static map<estring,estring> Values;
static estring CurrentTopic;
static unsigned LTime = 0;
static estring DMesg;
static SemaphoreHandle_t Sem = 0, Mtx = 0, DMtx = 0;


#ifdef CONFIG_SIGNAL_PROC
class FnMqttSend: public Function
{
	public:
	explicit FnMqttSend(const char *name, bool retain = false)
	: Function(name)
	, m_retain(retain)
	{ }

	void operator () (DataSignal *);
	int setParam(unsigned x, DataSignal *s);

	const char *type() const
	{ return FuncName; }

	static const char FuncName[];

	private:
	bool m_retain;	// currently not usable - no factory
};

const char FnMqttSend::FuncName[] = "mqtt_send";


int FnMqttSend::setParam(unsigned x, DataSignal *s)
{
	s->addFunction(this);
	return 0;
}


void FnMqttSend::operator () (DataSignal *s)
{
	if (s == 0)
		return;
	char buf[64];
	mstream str(buf,sizeof(buf));
	s->toStream(str);
	log_dbug(TAG,"FnMqttSend: %s %s",s->signalName(),str.c_str());
	mqtt_pub(s->signalName(),str.c_str(),str.size(),m_retain);
}
#endif


static void update_signal(const char *t, const void *d, size_t s)
{
	auto i = Values.find(t);
	if (i == Values.end()) {
		log_warn(TAG,"no value for topic %s",t);
		return;
	}
	i->second.assign((const char *)d,s);
	log_dbug(TAG,"topic %s update to '%.s'",t,s,d);
}


static void connect_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
	log_dbug(TAG,"connection status %d",status);
}


static void request_cb(void *arg, err_t e)
{
	xSemaphoreGive(Sem);
	//auto x = xSemaphoreGive(Sem);
	//assert(x == pdTRUE); Not! May fail on timeout!
}


static void subscribe_cb(void *arg, err_t e)
{
	const char *topic = (const char *)arg;
	log_dbug(TAG,"subscribe on %s returned %d",topic,e);
	xSemaphoreGive(Sem);
}


static void incoming_data_cb(void *arg, const uint8_t *data, uint16_t len, uint8_t flags)
{
	log_dbug(TAG,"incoming data for %s",CurrentTopic.c_str());
	auto i = Subscriptions.find(CurrentTopic);
	if (i != Subscriptions.end())
		i->second(CurrentTopic.c_str(),data,len);
	else
		log_warn(TAG,"unknown topic %s",CurrentTopic.c_str());
}


static void incoming_publish_cb(void *arg, const char *topic, unsigned len)
{
	CurrentTopic = topic;
	log_dbug(TAG,"incoming topic %s",topic);
}


static void mqtt_pub_uptime(void * = 0)
{
	if ((Client == 0) || !mqtt_client_is_connected(Client))
		return;
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
	if (t != LTime) {
		LTime = t;
		if (d > 0)
			n = snprintf(value,sizeof(value),"%u days, ",d);
		n += snprintf(value+n,sizeof(value)-n,"%d:%02u",h,m);
	}
	if ((n > 0) && (n <= sizeof(value))) {
		if (err_t e = mqtt_pub("uptime",value,n,0))
			log_error(TAG,"publish failed: %d",e);
		else
			log_dbug(TAG,"published uptime %s",value);
	}
}


static void mqtt_pub_dmesg(void *)
{
	xSemaphoreTake(DMtx,portMAX_DELAY);
	if (!DMesg.empty()) {
		mqtt_pub("dmesg",DMesg.c_str(),DMesg.size(),0);
		DMesg.clear();
	}
	xSemaphoreGive(DMtx);
}


int mqtt_pub(const char *t, const char *v, int len, int retain)
{
	if (Client == 0)
		return 1;
	const char *hn = Config.nodename().c_str();
	size_t hl = Config.nodename().size();
	size_t tl = strlen(t);
	char topic[hl+tl+2];
	memcpy(topic,hn,hl);
	topic[hl] = '/';
	memcpy(topic+hl+1,t,tl+1);
	if (pdFALSE == xSemaphoreTake(Mtx,20/portTICK_PERIOD_MS)) {
	//if (pdFALSE == xSemaphoreTake(Mtx,portMAX_DELAY)) {
		log_dbug(TAG,"publish '%s': mutex failure",topic);
		return 1;
	}
	err_t e = mqtt_publish(Client,topic,v,len,0,retain,request_cb,0);
	if (e) {
		log_dbug(TAG,"publish '%s' returned %d",topic,e);
	} else {
//		Waiting for the final result may hang long!
//		We want processing to continue.
//		Therefore we don't gather the final result.
//		Reporting the final result would require the callback
//		to refer to data on heap and do a cleanup...
//		But we should wait a little bit to take pressure
//		from the TCP stack.
		//auto x = xSemaphoreTake(Sem,20/portTICK_PERIOD_MS);
		auto x = xSemaphoreTake(Sem,portMAX_DELAY);
		log_dbug(TAG,"publish '%s' %s",topic,x ? "OK" : "timed out");
	}
	xSemaphoreGive(Mtx);
	return e;
}


int mqtt_sub(const char *t, void (*callback)(const char *,const void *,size_t))
{
	if (Mtx == 0)	// subscription should be possible before mqtt is initialized!
		Mtx = xSemaphoreCreateMutex();
	const char *hn = Config.nodename().c_str();
	size_t hl = strlen(hn);
	if (hl == 0) {
		log_error(TAG,"hostname not set");
		return -1;
	}
	size_t tl = strlen(t);
	char topic[hl+tl+2];
	memcpy(topic,hn,hl);
	topic[hl] = '/';
	memcpy(topic+hl+1,t,tl+1);
	log_dbug(TAG,"subscribe '%s'",topic);
	xSemaphoreTake(Mtx,portMAX_DELAY);
	Subscriptions.insert(make_pair((estring)topic,callback));
	err_t e = 1;
	if (Client != 0) {
		e = mqtt_sub_unsub(Client,topic,0,subscribe_cb,(void*)topic,1);
		if (e)
			log_warn(TAG,"subscribe '%s' returned %d",topic,e);
		else
			xSemaphoreTake(Sem,portMAX_DELAY);
	}
	xSemaphoreGive(Mtx);
	return e;

}


void mqtt_start(void)
{
	log_info(TAG,"start");
	if (Client != 0)
		log_warn(TAG,"already initialized");
	if (!Config.has_mqtt()) {
		log_warn(TAG,"not configured");
		return;
	}
	if (!Config.mqtt().enable()) {
		log_warn(TAG,"disabled");
		return;
	}
	if (!Config.mqtt().has_uri()) {
		log_warn(TAG,"no URI");
		return;
	}
	if (!wifi_station_isup()) {
		log_warn(TAG,"wifi station down");
		return;
	}
	if (!Config.has_nodename()) {
		log_error(TAG,"no nodename");
		Config.mutable_mqtt()->set_enable(false);
		return;
	}
	Lock lock(Mtx);
	mqtt_connect_client_info_t ci;
	memset(&ci,0,sizeof(ci));
	ci.client_id = Config.nodename().c_str();
	const MQTT &m = Config.mqtt();
	if (m.has_username())
		ci.client_user = m.username().c_str();
	if (m.has_password())
		ci.client_pass = m.password().c_str();
	const char *u = m.uri().c_str();
	if (strncmp(u,"mqtt://",7)) {
		log_error(TAG,"invalid URI format");
		return;
	}
	const char *h = u + 7;
	const char *c = strchr(h,':');
	long port = 1883;
	size_t hl;
	if (c != 0) {
		long l = strtol(c+1,0,0);
		if ((l <= 0) || (l > UINT16_MAX)) {
			log_error(TAG,"invalid port");
			return;
		}
		port = l;
		hl = c-h;
	} else
		hl = strlen(h);
	char hn[hl+1];
	memcpy(hn,h,hl);
	hn[hl] = 0;
	uint32_t ip4 = resolve_hostname(hn);
	if (ip4 == 0) {
		log_error(TAG,"unable to resolve host %s",hn);
		return;
	}
	ip_addr_t ip;
#if defined CONFIG_LWIP_IPV6 || defined CONFIG_IDF_TARGET_ESP32
	ip.type = IPADDR_TYPE_V4;
	ip.u_addr.ip4.addr = ip4;
#else
	ip.addr = ip4;
#endif
	ci.client_id = Config.nodename().c_str();
	ci.keep_alive = 30;
	if (Client == 0)
		Client = mqtt_client_new();
	if (err_t e = mqtt_client_connect(Client,&ip,port,connect_cb,0,&ci)) {
		log_error(TAG,"error connecting %d",e);
		return;
	}
	mqtt_set_inpub_callback(Client,incoming_publish_cb,incoming_data_cb,0);
	LTime = 0;
	mqtt_pub("version",VERSION,strlen(VERSION),0);
	for (const auto &s : Subscriptions) {
		const char *topic = s.first.c_str();
		if (err_t e = mqtt_sub_unsub(Client,topic,0,subscribe_cb,(void*)topic,1))
			log_error(TAG,"subscribe %s: %d",topic,e);
	}
}


void mqtt_stop(void)
{
	Lock lock(Mtx);
	if (Client) {
		mqtt_disconnect(Client);
		free(Client);
		Client = 0;
	}
}


static void mqtt_startstop(void *a)
{
	if ((int)a) 
		mqtt_start();
	else
		mqtt_stop();
}


void mqtt_set_dmesg(const char *m, size_t s)
{
	if (s > 120)
		s = 120;
	if (DMtx == 0)
		DMtx = xSemaphoreCreateMutex();
	xSemaphoreTake(DMtx,portMAX_DELAY);
	DMesg.assign(m,s);
	xSemaphoreGive(DMtx);
}


static void pub_element(JsonElement *e, const char *parent = "")
{
	char name[strlen(parent)+strlen(e->name())+2];
	if (parent[0]) {
		strcpy(name,parent);
		strcat(name,"/");
	} else {
		name[0] = 0;
	}
	strcat(name,e->name());
	char buf[128];
	mstream str(buf,sizeof(buf));
	e->writeValue(str);
	if (const char *dim = e->getDimension()) {
		str << ' ';
		str << dim;
	}
	log_dbug(TAG,"pub_element '%s'",str.c_str());
	mqtt_pub(name,str.c_str(),str.size(),0);
}


void mqtt_pub_rtdata(void *)
{
	if ((Client == 0) || !mqtt_client_is_connected(Client)) {
		if (Config.mqtt().enable())
			mqtt_start();
		return;
	}
	mqtt_pub_uptime();
	rtd_lock();
	JsonElement *e = RTData->first();
	while (e) {
		if (JsonObject *o = e->toObject()) {
			const char *n = e->name();
			JsonElement *c = o->first();
			while (c) {
				pub_element(c,n);
				c = c->next();
			}
		} else {
			pub_element(e);
		}
		e = e->next();
	}
	rtd_unlock();
}


int mqtt_setup(void)
{
	Client = 0;
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	if (DMtx == 0)
		DMtx = xSemaphoreCreateMutex();
	Sem = xSemaphoreCreateBinary();
	action_add("mqtt!pub_rtdata",mqtt_pub_rtdata,0,"publish run-time data via MQTT");
	action_add("mqtt!pub_dmesg",mqtt_pub_dmesg,0,"publish dmesg via MQTT");
#ifdef CONFIG_SIGNAL_PROC
	new FuncFact<FnMqttSend>;
#endif
	action_add("mqtt!stop",mqtt_startstop,0,"mqtt stop");
	Action *a = action_add("mqtt!start",mqtt_startstop,(void*)1,"mqtt start");
	event_callback(StationUpEv,a);

	if (Config.has_mqtt() && Config.mqtt().enable())
		mqtt_start();
	for (const auto &s : Config.mqtt().subscribtions())
		mqtt_sub(s.c_str(),update_signal);
	return 0;
}


int mqtt(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return arg_invnum(term);
	MQTT *m = Config.mutable_mqtt();
	if ((argc == 1) || (!strcmp(args[1],"status"))) {
		if (Config.has_mqtt()) {
			term.printf("URI: %s\n"
				"user: %s\n"
				"pass: %s\n"
				"%sabled, %sconnected\n"
				,m->uri().c_str()
				,m->username().c_str()
				,m->password().c_str()
				,m->enable() ? "en" : "dis"
				,Client && mqtt_client_is_connected(Client) ? "" : "not ");
		} else {
			term.printf("not configured\n");
		}
		return 0;
	}
	if (!strcmp(args[1],"uri")) {
		if (argc == 3)
			m->set_uri(args[2]);
		else
			return arg_invalid(term,args[1]);
	} else if (!strcmp(args[1],"user")) {
		if (argc == 3)
			m->set_username(args[2]);
		else if (argc == 2)
			m->clear_username();
		else
			return arg_invalid(term,args[1]);
	} else if (!strcmp(args[1],"pass")) {
		if (argc == 3)
			m->set_password(args[2]);
		else if (argc == 2)
			m->clear_password();
		else
			return arg_invalid(term,args[1]);
	} else if (!strcmp(args[1],"enable")) {
		m->set_enable(true);
	} else if (!strcmp(args[1],"disable")) {
		m->set_enable(false);
	} else if (!strcmp(args[1],"clear")) {
		m->clear();
	} else if (!strcmp(args[1],"start")) {
		mqtt_start();
	} else if (!strcmp(args[1],"stop")) {
		mqtt_stop();
	} else if (!strcmp(args[1],"sub")) {
		if (argc == 2) {
			for (const auto &s : Subscriptions)
				term.println(s.first.c_str());
		} else {
			for (const auto &s : m->subscribtions()) {
				if (s == args[2]) {
					term.println("already subscribed");
					return 1;
				}
			}
			m->add_subscribtions(args[2]);
			mqtt_sub(args[2],update_signal);
		}
	} else {
		return arg_invalid(term,args[1]);
	}
	return 0;
}


#endif // CONFIG_MQTT
