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

#include "actions.h"
#include "cyclic.h"
#include "event.h"
#include "log.h"
//#include "profiling.h"

#include <assert.h>
#include <string.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <vector>

using namespace std;


struct Event {
	event_t id;
	void *arg = 0;
	Event(event_t i = 0, void *a = 0)
	: id(i)
	, arg(a)
	{ }
};


static QueueHandle_t EventsQ = 0;
static SemaphoreHandle_t EventMtx = 0;
static vector<EventHandler> EventHandlers;
#define TAG MODULE_EVENT
static uint8_t Lost = 0;


char *concat(const char *s0, const char *s1)
{
	size_t l0 = strlen(s0);
	size_t l1 = strlen(s1) + 1;
	char *r = (char *)malloc(l0+l1);
	memcpy(r,s0,l0);
	memcpy(r+l0,s1,l1);
	return r;
}


event_t event_register(const char *cat, const char *type)
{
	const char *name;
	if (type == 0) {
		name = cat;
	} else {
		name = concat(cat,type);
	}
	if (0 == strchr(name,'`'))
		log_warn(TAG,"event '%s' missing `",name);
	Lock lock(EventMtx,__FUNCTION__);
	size_t n = EventHandlers.size();
	// event_t 0: name: empty string; invalid event, catched in this loop
	for (size_t i = 1; i < n; ++i) {
		if (!strcmp(name,EventHandlers[i].name)) {
			log_error(TAG,"duplicate event %d: %s",i,name);
			if (name != cat)
				free((void*)name);
			return (event_t) i;
		}
	}
	log_dbug(TAG,"register %s=%u",name,n);
	EventHandlers.emplace_back(name);
	return (event_t) n;
}


uint32_t event_occur(event_t e)
{
	if (e >= EventHandlers.size())
		return 0;
	return EventHandlers[e].occur;
}


uint64_t event_time(event_t e)
{
	if (e >= EventHandlers.size())
		return 0;
	return EventHandlers[e].time;
}


trigger_t event_callback(event_t e, Action *a)
{
	if (pdFALSE == xSemaphoreTake(EventMtx,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(EventMtx,__FUNCTION__);
	if (e && (e < EventHandlers.size())) {
		Callback c(a);
		trigger_t t = (trigger_t) ((e << 16) | EventHandlers[e].callbacks.size());
		EventHandlers[e].callbacks.push_back(c);
		xSemaphoreGive(EventMtx);
		log_dbug(TAG,"%s -> %s",EventHandlers[e].name,a->name);
		return t;
	}
	xSemaphoreGive(EventMtx);
	log_warn(TAG,"invalid event %u",e);
	return 0;
}


trigger_t event_callback_arg(event_t e, Action *a, const char *arg)
{
	if (pdFALSE == xSemaphoreTake(EventMtx,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(EventMtx,__FUNCTION__);
	if (e && (e < EventHandlers.size())) {
		Callback c(a,strdup(arg));
		trigger_t t = (trigger_t) ((e << 16) | EventHandlers[e].callbacks.size());
		EventHandlers[e].callbacks.push_back(c);
		xSemaphoreGive(EventMtx);
		log_dbug(TAG,"%s -> %s",EventHandlers[e].name,a->name);
		return t;
	}
	xSemaphoreGive(EventMtx);
	log_warn(TAG,"invalid event %u",e);
	return 0;
}


int event_trigger_en(trigger_t t, bool en)
{
	event_t e = t >> 16;
	uint16_t c = t;
	if ((e == 0) || (e >= EventHandlers.size()) ||  (c >= EventHandlers[e].callbacks.size())) {
		log_warn(TAG,"cannot %sable event %d",en?"en":"dis",t);
		return 1;
	}
	EventHandlers[e].callbacks[c].enabled = en;
	return 0;
}


trigger_t event_callback(const char *event, const char *action)
{
	const char *x = 0;
	if (event_t e = event_id(event)) {
		if (const char *sp = strchr(action,' ')) {
			++sp;
			char tmp[sp-action];
			memcpy(tmp,action,sizeof(tmp));
			if (Action *a = action_get(tmp))
				return event_callback_arg(e,a,strdup(sp));
		} else if (Action *a = action_get(action))
			return event_callback(e,a);
		x = action;
	} else {
		x = event;
	}
	log_warn(TAG,"callback arg invalid %s",x);
	return 0;
}


trigger_t event_callback_arg(const char *event, const char *action, const char *arg)
{
	const char *x = 0;
	if (event_t e = event_id(event)) {
		if (Action *a = action_get(action))
			return event_callback_arg(e,a,arg);
		else
			x = action;
	} else {
		x = event;
	}
	log_warn(TAG,"callback arg invalid %s",x);
	return 0;
}


const EventHandler *event_handler(event_t e)
{
	if (e < EventHandlers.size())
		return &EventHandlers[e];
	return 0;
}


int event_detach(event_t e, Action *a)
{
	const char *err = "invalid event";
	if (e != 0) {
		Lock lock(EventMtx,__FUNCTION__);
		if (e < EventHandlers.size()) {
			EventHandler &h = EventHandlers[e];
			for (size_t i = 0, j = h.callbacks.size(); i != j; ++i) {
				if (h.callbacks[i].action == a) {
					h.callbacks.erase(h.callbacks.begin()+i);
					return 0;
				}
			}
			err = "action not found";
		}
	}
	log_error(TAG,"detach %u: %s",e,err);
	return 1;
}


int event_detach(const char *event, const char *action)
{
	event_t e = event_id(event);
	Action *a = action_get(action);
	if ((e == 0) || (a == 0)) {
		log_warn(TAG,"detach('%s',%s'): invalid arg",event,action);
		return 2;
	}
	return event_detach(e,a);
}


event_t event_id(const char *n)
{
//	PROFILE_FUNCTION();
	Lock lock(EventMtx,__FUNCTION__);
	event_t r = 0;
	for (unsigned i = 1; i < EventHandlers.size(); ++i) {
		if (!strcmp(n,EventHandlers[i].name)) {
			r = (event_t) i;
			break;
		}
	}
	return r;
}



const char *event_name(event_t e)
{
	if (e != 0) {
		if (e < EventHandlers.size())
			return EventHandlers[e].name;
	}
	log_dbug(TAG,"invalid event %u",e);
	return 0;
}


void event_trigger(event_t id)
{
	if (id == 0) {
		log_warn(TAG,"trigger 0");
		return;
	}
	Event e(id);
	BaseType_t r = xQueueSend(EventsQ,&e,1000);
	if (r != pdTRUE) {
		log_dbug(TAG,"lost event %d",id);
		++Lost;
	} else
		log_dbug(TAG,"trigger %s",EventHandlers[id].name);
}


void event_trigger_nd(event_t id)	// no-debug version for syslog only
{
	Event e(id);
	BaseType_t r = xQueueSend(EventsQ,&e,1000);
	if (r != pdTRUE)
		++Lost;
}


void event_trigger_arg(event_t id, void *arg)
{
	if (id != 0) {
		Event e(id,arg);
		log_dbug(TAG,"trigger %d %p",id,arg);
		BaseType_t r = xQueueSend(EventsQ,&e,1000);
		if (r != pdTRUE)
			++Lost;
	}
}


void event_isr_trigger(event_t id)
{
	// ! don't log from ISR
	if (id != 0) {
		Event e(id);
		BaseType_t r = xQueueSendFromISR(EventsQ,&e,0);
		if (r != pdTRUE)
			++Lost;
	}
}


void event_isr_trigger_arg(event_t id, void *arg)
{
	// ! don't log from ISR
	if (id != 0) {
		Event e(id,arg);
		BaseType_t r = xQueueSendFromISR(EventsQ,&e,0);
		if (r != pdTRUE)
			++Lost;
	}
}


static void event_task(void *)
{
#ifdef ESP32
	#ifdef CONFIG_VERIFY_HEAP
	#define dt 100
	#else
	#define dt portMAX_DELAY
	#endif // CONFIG_VERIFY_HEAP
#else
	unsigned d = 1;
#define dt (d * portTICK_PERIOD_MS)
#endif
	for (;;) {
		Event e;
		BaseType_t r = xQueueReceive(EventsQ,&e,dt);
		if (Lost) {
			log_warn(TAG,"lost %u events",(unsigned)Lost);
			Lost = 0;
		}
		if (r == pdFALSE) {
			// timeout: process cyclic
#ifdef ESP32
			// cyclic has its own task
	#ifdef CONFIG_VERIFY_HEAP
			heap_caps_check_integrity_all(true);
	#endif
#else
			d = cyclic_execute();
#endif
			if (e.id == 0)
				continue;
		}
//		con_printf("event %d,%x\n",e.id,e.arg);
		MLock lock(EventMtx,__FUNCTION__);
		int64_t start = esp_timer_get_time();
		if (e.id < EventHandlers.size()) {
			EventHandler &h = EventHandlers[e.id];
			++h.occur;
			if (!h.callbacks.empty()) {
				log_local(TAG,"%s callbacks, arg %p",h.name,e.arg);
				// need to copy the enabled callbacks,
				// because the enabling might be changed
				// with an action
				unsigned n = 0;
				for (const auto &c : h.callbacks)
					if (c.enabled)
						++n;
				Callback cb[n];
				n = 0;
				for (const auto &c : h.callbacks)
					if (c.enabled)
						cb[n++] = c;
				for (const auto &c : cb) {
					if (c.enabled) {
						lock.unlock();
						log_local(TAG,"\t%s, arg %-16s",c.action->name,c.arg ? c.arg : "''");
						c.action->activate(e.arg ? e.arg : (c.arg ? strdup((const char *)c.arg) : 0));
						lock.lock();
					}
				}
				int64_t end = esp_timer_get_time();
				log_local(TAG,"%s time: %lu",h.name,end-start);
				h.time += end-start;
			} else {
				log_local(TAG,"%s %p",EventHandlers[e.id].name,e.arg);
			}
		} else {
			log_warn(TAG,"invalid event %u",e);
		}
		if (e.arg) {
//			con_printf("event %d, arg %s\n",e.id,e.arg);
			free(e.arg);
		}
	}
}


void event_init(void)
{
	EventMtx = xSemaphoreCreateMutex();
	EventHandlers.emplace_back("<null>");	// (event_t)0 is an invalid event
	EventHandlers.emplace_back("init`done");
	EventHandlers.emplace_back("wifi`station_up");
	EventHandlers.emplace_back("wifi`station_down");
	EventsQ = xQueueCreate(32,sizeof(Event));
}


int event_start(void)
{
	BaseType_t r = xTaskCreatePinnedToCore(&event_task, "events", 4096, (void*)0, 9, NULL, 1);
	if (r != pdPASS) {
		log_error(TAG,"create task: %d",r);
		return 1;
	}
	return 0;
}
