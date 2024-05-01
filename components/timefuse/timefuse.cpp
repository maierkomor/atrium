/*
 *  Copyright (C) 2020-2023, Thomas Maier-Komor
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

#if 0
#define log_devel log_dbug
#else
#define log_devel(...)
#endif

#define TIMER_NAME_SIZE 10

struct Timer
{
	TimerHandle_t id;
	event_t start,stop,timeout;
	char name[TIMER_NAME_SIZE];
};


#define TAG MODULE_TIMEFUSE
static SemaphoreHandle_t Mtx = 0;
static Timer *Timers = 0;
uint16_t NumTimers = 0, NumAlloc = 0;


static void trigger_timeout(TimerHandle_t h)
{
	event_trigger((event_t)(int)pvTimerGetTimerID(h));
}


const char *timefuse_name(timefuse_t t)
{
	if ((t > 0) || (t <= NumTimers))
		return Timers[t].name;
	return "";
}


timefuse_t timefuse_iterator()
{
	return NumTimers != 0;
}


timefuse_t timefuse_next(timefuse_t t)
{
	if (t != 0) {
		++t;
		if (t > NumTimers)
			t = 0;
	}
	return t;
}


static timefuse_t timefuse_get_nl(const char *n)
{
	Timer *b = Timers;
	auto i = b+1;
	Timer *e = b+NumTimers+1;
	while (i != e) {
		if (0 == strcmp(n,i->name)) {
			log_devel(TAG,"get: %s => %d",n,i-b);
			return i-b;
		}
		++i;
	}
	log_devel(TAG,"unknown timer %s",n);
	return 0;
}


timefuse_t timefuse_get(const char *n)
{
	Lock lock(Mtx);
	return timefuse_get_nl(n);
}


int timefuse_start(timefuse_t t)
{
	int r = 1;
	if ((t > 0) && (t <= NumTimers)) {
		Lock lock(Mtx);
		Timer *x = Timers+t;
		if (pdTRUE == xTimerStart(x->id,portMAX_DELAY)) {
			event_trigger(x->start);
			r = 0;
			log_devel(TAG,"started %d, %s",t,x->name);
		} else {
			log_devel(TAG,"start %d failed",t);
		}
	}
	if (r) {
		log_devel(TAG,"start %d invalid",t);
	}
	return r;
}


int timefuse_active(timefuse_t t)
{
	if ((t > 0) && (t <= NumTimers))
		return xTimerIsTimerActive(Timers[t].id);
	return -1;
}


int timefuse_start(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return timefuse_start(t);
	return -1;
}


/*
int timefuse_delete(timefuse_t t)
{
	// Not supported - would be incomplete
	// deletion of associated events and timers missing
}


int timefuse_delete(const char *n)
{
	// Not supported - would be incomplete
	// deletion of associated events and timers missing
}
*/


int timefuse_stop(timefuse_t t)
{
	int r = 1;
	{
		Lock lock(Mtx);
		if ((t > 0) && (t <= NumTimers)) {
			if (pdTRUE == xTimerStop(Timers[t].id,portMAX_DELAY)) {
				log_devel(TAG,"timer %s stopped",Timers[t].name);
				event_trigger(Timers[t].stop);
			} else {
				log_devel(TAG,"timer %s stop failed",Timers[t].name);
			}
			r = 0;
		} else {
			log_devel(TAG,"stop invalid %d",t);
		}
	}
	return r;
}


int timefuse_stop(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return timefuse_stop(t);
	log_devel(TAG,"stop: timer %s unknown",n);
	return 1;
}


event_t timefuse_start_event(timefuse_t t)
{
	if ((t > 0) && (t <= NumTimers))
		return Timers[t].start;
	return 0;
}


event_t timefuse_stop_event(timefuse_t t)
{
	if ((t > 0) && (t <= NumTimers))
		return Timers[t].stop;
	return 0;
}


event_t timefuse_timeout_event(timefuse_t t)
{
	if ((t > 0) && (t <= NumTimers))
		return Timers[t].timeout;
	return 0;
}


event_t timefuse_start_event(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return Timers[t].start;
	log_devel(TAG,"start: timer %s unknown",n);
	return 0;
}


event_t timefuse_stop_event(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return Timers[t].stop;
	log_devel(TAG,"start: timer %s unknown",n);
	return 0;
}


event_t timefuse_timeout_event(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return Timers[t].timeout;
	log_devel(TAG,"start: timer %s unknown",n);
	return 0;
}


unsigned timefuse_interval_get(timefuse_t t)
{
	unsigned ticks = 0;
	Lock lock(Mtx);
	if ((t > 0) && (t <= NumTimers))
		ticks = xTimerGetPeriod(Timers[t].id);
	return ticks * portTICK_PERIOD_MS;
}


unsigned timefuse_interval_get(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return timefuse_interval_get(t);
	return 0;
}


int timefuse_interval_set(timefuse_t t, unsigned i)
{
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	if (i % portTICK_PERIOD_MS) {
		log_warn(TAG,"invalid interval of %ums",i);
		return 1;
	}
#endif
	if ((t > 0) && (t <= NumTimers))
		return pdFALSE == xTimerChangePeriod(Timers[t].id,i/portTICK_PERIOD_MS,portMAX_DELAY);
	return 1;
}


int timefuse_interval_set(const char *n, unsigned i)
{
	return timefuse_interval_set(timefuse_get(n),i);
}


#ifdef ESP32
int timefuse_repeat_get(timefuse_t t)
{
	if ((t > 0) && (t <= NumTimers))
		return uxTimerGetReloadMode(Timers[t].id);
	return -1;
}


int timefuse_repeat_get(const char *n)
{
	if (timefuse_t t = timefuse_get(n))
		return timefuse_repeat_get(t);
	return -1;
}


int timefuse_repeat_set(timefuse_t t, bool r)
{
	if ((t > 0) && (t <= NumTimers)) {
		vTimerSetReloadMode(Timers[t].id,r);
		return 0;
	}
	return -1;
}


int timefuse_repeat_set(const char *n, bool r)
{
	if (timefuse_t t = timefuse_get(n))
		return timefuse_repeat_set(t,r);
	return -1;
}
#endif // ESP32


static void start_action(void *arg)
{
	timefuse_t t = (timefuse_t)(unsigned)arg;
	timefuse_start(t);

}


static void stop_action(void *arg)
{
	timefuse_t t = (timefuse_t)(unsigned)arg;
	timefuse_stop(t);
}


timefuse_t timefuse_create(const char *n, unsigned d_ms, bool repeat)
{
	size_t nl = n == 0 ? UINT32_MAX : strlen(n);
	if (nl >= TIMER_NAME_SIZE) {
		log_warn(TAG,"invalid name");
		return 0;
	}
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	if ((d_ms % portTICK_PERIOD_MS) || (pdMS_TO_TICKS(d_ms) == 0)) {
		log_warn(TAG,"invalid interval %ums",d_ms);
		return 0;
	}
#endif
	unsigned t = 0;
	Lock lock(Mtx);
	if (0 != timefuse_get_nl(n)) {
		log_warn(TAG,"%s already defined",n);
	} else {
		log_dbug(TAG,"create %s, id %u",n,NumTimers+1);
		if (NumTimers+1 >= NumAlloc) {
			NumAlloc += 4;
			Timer *tmp = (Timer *) realloc(Timers,sizeof(Timer)*NumAlloc);
			if (tmp == 0)
				return 0;
			Timers = tmp;
		}
		t = ++NumTimers;
		Timers[t].start = event_register(n,"`started");
		Timers[t].stop = event_register(n,"`stopped");
		event_t timeout = event_register(n,"`timeout");
		Timers[t].timeout = timeout;
		Timers[t].id = xTimerCreate(0,pdMS_TO_TICKS(d_ms),repeat,(void*)(int)timeout,trigger_timeout);
		memcpy(Timers[t].name,n,nl+1);
		action_add(concat(n,"!start"),start_action,(void*)t,"start this timefuse");
		action_add(concat(n,"!stop"),stop_action,(void*)t,"stop this timefuse");
	}
	return t;
}


void timefuse_setup()
{
	Mtx = xSemaphoreCreateMutex();
	NumAlloc = 4;
	Timers = (Timer *) malloc(sizeof(Timer)*NumAlloc);
	bzero(Timers,sizeof(Timer));
}
