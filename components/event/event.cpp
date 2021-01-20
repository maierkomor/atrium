/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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
#include "event.h"
#include "log.h"

#include <assert.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <vector>

using namespace std;


struct EventHandler
{
	explicit EventHandler(const char *n)
	: name(n)
	{ } 

	const char *name;	// name of event
	vector<Action *> callbacks;
};


static QueueHandle_t EventsQ = 0;
static SemaphoreHandle_t Mtx = 0;
static vector<EventHandler> EventHandlers;
static char TAG[] = "event";


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
		log_warn(TAG,"event name '%s' does not match convention",name);
	Lock lock(Mtx);
	size_t n = EventHandlers.size();
	// event_t 0: name: empty string; invalid event, catched in this loop
	for (size_t i = 0; i < n; ++i) {
		if (!strcmp(name,EventHandlers[i].name)) {
			log_warn(TAG,"event %s is already registered",name);
			if (name != cat)
				free((void*)name);
			return (event_t) i;
		}
	}
	assert(n <= EVENT_T_MAX);
	EventHandlers.emplace_back(name);
	return (event_t) n;
}


int event_callback(event_t e, Action *a)
{
	assert(Mtx);
	xSemaphoreTake(Mtx,portMAX_DELAY);
	if (e < EventHandlers.size()) {
		EventHandlers[e].callbacks.push_back(a);
		xSemaphoreGive(Mtx);
		log_dbug(TAG,"event %s -> action %s",EventHandlers[e].name,a->name);
		return 0;
	}
	xSemaphoreGive(Mtx);
	log_error(TAG,"cannot register callback on event %u: event out of range",e);
	return 1;
}


int event_callback(const char *event, const char *action)
{
	event_t e = event_id(event);
	if (e == 0) {
		log_warn(TAG,"event_callback('%s',%s'): unknown event",event,action);
		return 1;
	}
	Action *a = action_get(action);
	if (a == 0) {
		log_warn(TAG,"event_callback('%s',%s'): unknown action",event,action);
		return 2;
	}
	return event_callback(e,a);
}


const std::vector<Action *> &event_callbacks(event_t e)
{
	return EventHandlers[e].callbacks;
}


int event_detach(event_t e, Action *a)
{
	const char *err = "invalid event";
	if (e != 0) {
		Lock lock(Mtx);
		if (e < EventHandlers.size()) {
			for (auto i = EventHandlers[e].callbacks.begin(), j = EventHandlers[e].callbacks.end(); i != j; ++i) {
				if (*i == a) {
					EventHandlers[e].callbacks.erase(i);
					return 0;
				}
			}
			err = "action not found";
		}
	}
	log_error(TAG,"cannot detach from event %u: %s",e,err);
	return 1;
}


event_t event_id(const char *n)
{
	{
		Lock lock(Mtx);
		for (unsigned i = 1; i < EventHandlers.size(); ++i)
			if (!strcmp(n,EventHandlers[i].name))
				return (event_t) i;
	}
	log_dbug(TAG,"unknown event %s",n);
	return 0;
}



const char *event_name(event_t e)
{
	if (e != 0) {
		Lock lock(Mtx);
		if (e < EventHandlers.size())
			return EventHandlers[e].name;
	}
	log_dbug(TAG,"invalid event id %d",e);
	return 0;
}


void event_trigger(event_t e)
{
	if (e == 0)
		return;
	log_dbug(TAG,"trigger event %d",e);
	BaseType_t r = xQueueSend(EventsQ,&e,portMAX_DELAY);
	assert(r == pdTRUE);
}


void event_isr_trigger(event_t e)
{
	// ! don't log from ISR
	if (e == 0)
		return;
	BaseType_t r = xQueueSendFromISR(EventsQ,&e,0);
 	if (r != pdTRUE)
		log_fatal(TAG,"send from ISR: %d",r);
 }
 
 
static void events_task(void *)
{
	for (;;) {
		event_t e;
		BaseType_t r = xQueueReceive(EventsQ,&e,portMAX_DELAY);
		assert(r == pdTRUE);
		Lock lock(Mtx);
		if (e < EventHandlers.size()) {
			log_dbug(TAG,"execute callbacks of %s",EventHandlers[e].name);
			for (auto a : EventHandlers[e].callbacks) {
				log_dbug(TAG,"\tfunction %s",a->name);
				a->func(a->arg);
			}
		} else {
			log_error(TAG,"invalid/unknown event_t %u",e);
		}
	}
}


int event_setup(void)
{
	EventHandlers.emplace_back("");	// (event_t)0 is an invalid event
	EventsQ = xQueueCreate(32,sizeof(event_t));
	Mtx = xSemaphoreCreateMutex();
	BaseType_t r = xTaskCreatePinnedToCore(&events_task, "events", 4096, (void*)0, 20, NULL, 1);
	if (r != pdPASS) {
		log_error(TAG,"task creation failed: freertos error %d",r);
		return 1;
	}
	return 0;
}
