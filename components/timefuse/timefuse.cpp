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

#include "actions.h"
#include "event.h"
#include "log.h"
#include "timefuse.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>


struct Timer
{
	Timer(const char *n, TimerHandle_t i, event_t s, event_t t, event_t o, Timer *x)
	: name(n)
	, next(x)
	, id(i)
	, start(s)
	, stop(t)
	, timeout(o)
	{
	}

	const char *name;
	Timer *next;
	TimerHandle_t id;
	event_t start,stop,timeout;
};


#define TAG MODULE_TIMEFUSE
static SemaphoreHandle_t Mtx = 0;
static Timer *Timers = 0;


static void trigger_timeout(TimerHandle_t h)
{
	event_trigger((event_t)(int)pvTimerGetTimerID(h));
}


const char *timefuse_name(timefuse_t t)
{
	if (t == 0)
		return "";
	return t->name;
}


timefuse_t timefuse_iterator()
{
	return Timers;
}


timefuse_t timefuse_next(timefuse_t t)
{
	if (t == 0)
		return 0;
	return t->next;
}


timefuse_t timefuse_get(const char *n)
{
	Lock lock(Mtx);
	Timer *t = Timers;
	while (t) {
		if (0 == strcmp(n,t->name))
			return t;
		t = t->next;
	}
	return 0;
}


int timefuse_start(timefuse_t t)
{
	log_dbug(TAG,"start %d",t);
	BaseType_t r = xTimerStart(t->id,portMAX_DELAY);
	if (pdTRUE == r) {
		event_trigger(t->start);
		return 0;
	}
	log_warn(TAG,"start error %d",r);
	return 1;
}


int timefuse_active(timefuse_t t)
{
	return xTimerIsTimerActive(t->id);
}


int timefuse_start(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return timefuse_start(t);
	log_warn(TAG,"timefuse_start('%s'): invalid timefuse",n);
	return 1;
}


int timefuse_delete(timefuse_t t)
{
	// incomplete - deletion of associated events and timers missing
	Timer *d;
	if (t == Timers) {
		d = Timers;
		Timers = d->next;
	} else {
		Timer *x = Timers;
		while ((x != 0) && (x->next != t)) 
			x = x->next;
		if (x == 0)
			return 1;
		d = x->next;
		x->next = d->next;
	}
	xTimerDelete(t->id,portMAX_DELAY);
	delete d;
	return 0;
}


int timefuse_delete(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return timefuse_delete(t);
	log_warn(TAG,"timefuse_delete('%s'): invalid timefuse",n);
	return 1;
}


int timefuse_stop(timefuse_t t)
{
	BaseType_t r = xTimerStop(t->id,portMAX_DELAY);
	if (pdTRUE == r) {
		event_trigger(t->stop);
		return 0;
	}
	log_warn(TAG,"stop failure %d",r);
	return 1;
}


int timefuse_stop(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return timefuse_stop(t);
	log_warn(TAG,"timefuse_stop('%s'): invalid timefuse",n);
	return 1;
}


event_t timefuse_start_event(timefuse_t t)
{
	if (t == 0)
		return 0;
	return t->start;
}


event_t timefuse_stop_event(timefuse_t t)
{
	if (t == 0)
		return 0;
	return t->stop;
}


event_t timefuse_timeout_event(timefuse_t t)
{
	if (t == 0)
		return 0;
	return t->timeout;
}


event_t timefuse_start_event(const char *n)
{
	return timefuse_start_event(timefuse_get(n));
}


event_t timefuse_stop_event(const char *n)
{
	return timefuse_stop_event(timefuse_get(n));
}


event_t timefuse_timeout_event(const char *n)
{
	return timefuse_timeout_event(timefuse_get(n));
}


unsigned timefuse_interval_get(timefuse_t t)
{
	unsigned ticks = xTimerGetPeriod(t->id);
	return ticks * portTICK_PERIOD_MS;
}


unsigned timefuse_interval_get(const char *n)
{
	return timefuse_interval_get(timefuse_get(n));
}


int timefuse_interval_set(timefuse_t t, unsigned i)
{
	if (i % portTICK_PERIOD_MS) {
		log_warn(TAG,"invalid interval of %ums",i);
		return 1;
	}
	return pdFALSE == xTimerChangePeriod(t->id,i/portTICK_PERIOD_MS,portMAX_DELAY);
}


int timefuse_interval_set(const char *n, unsigned i)
{
	return timefuse_interval_set(timefuse_get(n),i);
}


/*
bool timefuse_repeat_get(timefuse_t t)
{
	return uxTimerGetReloadMode(t->id);
}


bool timefuse_repeat_get(const char *n)
{
	return timefuse_repeat_get(timefuse_get(n));
}


int timefuse_repeat_set(timefuse_t t, bool r)
{
	vTimerSetReloadMode(t->id,r);
	return 0;
}


int timefuse_repeat_set(const char *n, bool r)
{
	timefuse_repeat_set(timefuse_get(n),r);
	return 0;
}
*/


static void start_action(void *arg)
{
	Timer *t = (Timer *)arg;
	timefuse_start(t);

}


static void stop_action(void *arg)
{
	Timer *t = (Timer *)arg;
	timefuse_stop(t);
}


timefuse_t timefuse_create(const char *n, unsigned d_ms, bool repeat)
{
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	if (0 != timefuse_get(n)) {
		log_warn(TAG,"timer %s already defined",n);
		return 0;
	}
	if ((d_ms % portTICK_PERIOD_MS) || (pdMS_TO_TICKS(d_ms) == 0)) {
		log_warn(TAG,"invalid timer interval %ums",d_ms);
		return 0;
	}
	log_dbug(TAG,"create %s",n);
	event_t started = event_register(n,"`started");
	event_t stopped = event_register(n,"`stopped");
	event_t timeout = event_register(n,"`timeout");
	if ((timeout == 0) || (stopped == 0) || (started == 0))
		return 0;
	TimerHandle_t id = xTimerCreate(n,pdMS_TO_TICKS(d_ms),repeat,(void*)(int)timeout,trigger_timeout);
	if (id == 0) {
		log_warn(TAG,"create %s failed",n);
		return 0;
	}
	Lock lock(Mtx);
	Timer *t = new Timer(strdup(n),id,started,stopped,timeout,Timers);
	if (t != 0) {
		action_add(concat(n,"!start"),start_action,t,"start this timefuse");
		action_add(concat(n,"!stop"),stop_action,t,"stop this timefuse");
		Timers = t;
	}
	return t;
}


