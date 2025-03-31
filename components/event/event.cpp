/*
 *  Copyright (C) 2020-2024, Thomas Maier-Komor
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
#include "terminal.h"

#include <assert.h>
#include <string.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <atomic>
#include <vector>

using namespace std;

#define TAG MODULE_EVENT
#define STATIC_TASK

#if !defined APP_CPU_NUM || defined CONFIG_FREERTOS_UNICORE
#define EVENT_CPU_NUM 0
#else
#define EVENT_CPU_NUM APP_CPU_NUM
#endif

#if 0
#define log_devel log_dbug
#else
#define log_devel(...)
#endif

// WARNING: no debug support on 1MB devices!!!
#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
#define log_dbug(...)
#define busy_set(...)
#elif defined CONFIG_LEDS
extern "C" void busy_set(int on);
#else
#define busy_set(...)
#endif

#ifndef CONFIG_EVENT_STACK_SIZE
#define CONFIG_EVENT_STACK_SIZE 8192
#endif


struct Event {
	event_t id;
	void *arg = 0;
	Event(event_t i = 0, void *a = 0)
	: id(i)
	, arg(a)
	{ }
};

#if defined STATIC_TASK && !defined CONFIG_IDF_TARGET_ESP8266
static StackType_t EventStack[CONFIG_EVENT_STACK_SIZE];
static StaticTask_t EventTask;
#endif

static QueueHandle_t EventsQ = 0;
static SemaphoreHandle_t EventMtx = 0;
static vector<EventHandler> EventHandlers;
#ifdef ESP32
static atomic<uint32_t> Lost, Discarded, Invalid, Processed, Isr;
#else
static uint32_t Lost = 0, Discarded = 0, Invalid = 0, Processed = 0, Isr = 0;
#endif


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
			log_warn(TAG,"duplicate event %d: %s",i,name);
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
	/*
	trigger_t t = 0;
	if (pdFALSE == xSemaphoreTake(EventMtx,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(EventMtx,__FUNCTION__);
	if ((e != 0) && (e < EventHandlers.size())) {
		Callback c(a);
		t = (trigger_t) ((e << 16) | EventHandlers[e].callbacks.size());
		EventHandlers[e].callbacks.push_back(c);
	}
	xSemaphoreGive(EventMtx);
	if (t) {
		log_dbug(TAG,"add %s -> %s",EventHandlers[e].name,a->name);
	} else {
		log_dbug(TAG,"callback got invalid event %u",e);
	}
	return t;
	*/
	return event_callback_arg(e,a,0);
}


trigger_t event_callback_arg(event_t e, Action *a, const char *arg)
{
	trigger_t t = 0;
	Lock lock(EventMtx);
	if ((e != 0) && (e < EventHandlers.size())) {
		Callback c(a,arg ? strdup(arg) : 0);	// copy arg
		t = (trigger_t) ((e << 16) | EventHandlers[e].callbacks.size());
		EventHandlers[e].callbacks.push_back(c);
	}
	if (t) {
		log_dbug(TAG,"add %s -> %s",EventHandlers[e].name,a->name);
	} else {
		log_dbug(TAG,"callback_arg got invalid event %u",e);
	}
	return t;
}


int event_trigger_en(trigger_t t, bool en)
{
	event_t e = t >> 16;
	uint16_t c = t;
	if ((e == 0) || (e >= EventHandlers.size()) ||  (c >= EventHandlers[e].callbacks.size())) {
		log_dbug(TAG,"invalid trigger %u",t);
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
			tmp[sp-action-1] = 0;
			if (Action *a = action_get(tmp))
				return event_callback_arg(e,a,sp);
		} else if (Action *a = action_get(action))
			return event_callback_arg(e,a,0);
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


const char *trigger_get_eventname(trigger_t t)
{
	event_t e = t >> 16;
	return event_name(e);
}


const char *trigger_get_actionname(trigger_t t)
{
	event_t e = t >> 16;
	unsigned id = t & 0xffff;
	return EventHandlers[e].callbacks[id].action->name;
}


const char *trigger_get_arg(trigger_t t)
{
	event_t e = t >> 16;
	unsigned id = t & 0xffff;
	return EventHandlers[e].callbacks[id].arg;
}


const EventHandler *event_handler(event_t e)
{
	if ((0 < e) && (e < EventHandlers.size()))
		return &EventHandlers[e];
	return 0;
}


int event_detach(event_t e, Action *a)
{
	const char *err = "invalid event";
	if ((e != 0) && (e < EventHandlers.size())) {
		Lock lock(EventMtx,__FUNCTION__);
		EventHandler &h = EventHandlers[e];
		for (size_t i = 0, j = h.callbacks.size(); i != j; ++i) {
			if (h.callbacks[i].action == a) {
				h.callbacks.erase(h.callbacks.begin()+i);
				return 0;
			}
		}
		err = "action not found";
	}
	log_warn(TAG,"detach %u: %s",e,err);
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


void IRAM_ATTR event_isr_handler(void *arg)
{
	event_t id = (event_t)(unsigned)arg;
	if (id != 0) {
		Event e(id);
		if (pdTRUE == xQueueSendFromISR(EventsQ,&e,0))
			++Isr;
		else
			++Lost;
	}
}


const char *event_name(event_t e)
{
	const char *name;
	if ((e != 0) && (e < EventHandlers.size())) {
		name = EventHandlers[e].name;
	} else {
		name = 0;
	}
	return name;
}


void event_trigger(event_t id)
{
	if ((id == 0) || (id >= EventHandlers.size())) {
		++Invalid;
		log_warn(TAG,"invalid id %d",id);
	} else if (EventHandlers[id].callbacks.empty()) {
		++Discarded;
		++EventHandlers[id].occur;
	} else {
		Event e(id);
		BaseType_t r = xQueueSend(EventsQ,&e,1000);
		if (r != pdTRUE) {
			++Lost;
		} else {
			log_dbug(TAG,"trigger %s",EventHandlers[id].name);
		}
	}
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
	if ((id != 0) && (id < EventHandlers.size())) {
		if (!EventHandlers[id].callbacks.empty()) {
			Event e(id,arg);
			BaseType_t r = xQueueSend(EventsQ,&e,1000);
			if (r != pdTRUE)
				++Lost;
			else
				log_dbug(TAG,"trigger %d %p",id,arg);
			return;
		}
		++Discarded;
		++EventHandlers[id].occur;
	} else {
		log_warn(TAG,"invalid id %d",id);
		++Invalid;
	}
	free(arg);
}


void IRAM_ATTR event_isr_trigger(event_t id)
{
	// ! don't log from ISR
	if (id != 0) {
		Event e(id);
		if (pdTRUE == xQueueSendFromISR(EventsQ,&e,0))
			++Isr;
		else
			++Lost;
	}
}


void IRAM_ATTR event_isr_trigger_arg(event_t id, void *arg)
{
	// ! don't log from ISR
	if (id != 0) {
		Event e(id,arg);
		if (pdTRUE == xQueueSendFromISR(EventsQ,&e,0))
			++Isr;
		else
			++Lost;
	}
}

static void event_task(void *)
{
#ifdef ESP32
	#define dt 1000
#else
	unsigned d = portTICK_PERIOD_MS;
	#define dt (d / portTICK_PERIOD_MS)
#endif
	uint32_t invalid = 0, lost = 0, discarded = 0;
	for (;;) {
		Event e;
		BaseType_t r = xQueueReceive(EventsQ,&e,dt);
		if (r == pdFALSE) {
			// timeout: process cyclic
			// cyclic has its own task
#ifndef ESP32
			d = cyclic_execute();
#endif
#ifdef CONFIG_VERIFY_HEAP
			heap_caps_check_integrity_all(true);
#endif
			// low prio stuff
			if (Lost != lost) {
				log_warn(TAG,"%u lost",Lost-lost);
				lost = Lost;
			}
			if (Invalid != invalid) {
				log_warn(TAG,"%u invalid",Invalid-invalid);
				invalid = Invalid;
			}
			if (Discarded != discarded) {
				log_dbug(TAG,"%u discarded",Discarded-discarded);
				discarded = Discarded;
			}
			continue;
		}
		MLock lock(EventMtx,__FUNCTION__);
		int64_t start = esp_timer_get_time();
		log_devel(TAG,"process %d",e.id);
		if (e.id < EventHandlers.size()) {
			EventHandler &h = EventHandlers[e.id];
			++h.occur;
			if (!h.callbacks.empty()) {
				busy_set(true);
				log_local(TAG,"%s: %u callbacks, arg %p",h.name,h.callbacks.size(),e.arg);
				// need to copy the enabled callbacks,
				// because the enabling might be changed
				// with an action
				bool enabled[h.callbacks.size()];
				unsigned x = 0, y = sizeof(enabled);
				for (const auto &c : h.callbacks) {
					enabled[x] = c.enabled;
					if (enabled[x] && (y == sizeof(enabled))) {
						y = x;
						++Processed;
					}
					++x;
				}
				if (y == sizeof(enabled))
					++Discarded;
				// after unlock 'EventHandler h' may be
				// a wild pointer! Therefore, reinit it
				// from its vector every iteration.
				for (size_t n = sizeof(enabled); y != n; ++y) {
				// action arg	: set on action_add (cannot be overwritten)
				// callback arg	: set on event_callback_arg
				// event arg    : set on event_trigger_arg
					if (enabled[y]) {
						const auto &c = EventHandlers[e.id].callbacks[y];
						log_devel(TAG,"\t%s, %s-arg %-16s",c.action->name?c.action->name:"<null>",e.arg?"event": c.arg?"callback":"null",e.arg ? e.arg : c.arg ? c.arg : "");
						Action *a = c.action;
						void *arg = e.arg ? e.arg : c.arg;
						lock.unlock();
						a->activate(arg);
						lock.lock();
					}
				}
				int64_t end = esp_timer_get_time();
				EventHandler &h2 = EventHandlers[e.id];
				log_local(TAG,"%s time: %lu",h2.name,end-start);
				h2.time += end-start;
				busy_set(false);
			} else {
				++Discarded;
			}
		} else {
			log_local(TAG,"invalid event %d",e.id);
			++Invalid;
		}
		if (e.arg) {
			log_devel(TAG,"free arg %p",e.arg);
			free(e.arg);
		}
		log_devel(TAG,"finished %d",e.id);
	}
}


void event_status(Terminal &t)
{
#ifdef ESP32
	t.printf("%u processed, %u discarded, %u lost, %u invalid, %u ISR\n"
			,Processed.load(),Discarded.load(),Lost.load(),Invalid.load(),Isr.load());
#else
	t.printf("%u processed, %u discarded, %u lost, %u invalid, %u ISR\n"
			,Processed,Discarded,Lost,Invalid,Isr);
#endif
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
#ifdef CONFIG_IDF_TARGET_ESP8266
	// static task allocation support is missing
	xTaskCreate(&event_task, "events", CONFIG_EVENT_STACK_SIZE, (void*)0, 9, 0);
#elif defined STATIC_TASK
	xTaskCreateStatic(&event_task, "events", sizeof(EventStack), (void*)0, 9, EventStack, &EventTask);
#else
	BaseType_t r = xTaskCreatePinnedToCore(&event_task, "events", 8*1024, (void*)0, 9, NULL, EVENT_CPU_NUM);
	if (r != pdPASS) {
		log_error(TAG,"create task: %d",r);
		return 1;
	}
#endif
	return 0;
}
