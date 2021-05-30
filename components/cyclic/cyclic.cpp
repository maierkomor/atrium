/*
 *  Copyright (C) 2017-2021, Thomas Maier-Komor
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

#include "cyclic.h"
#include "log.h"
#include "strstream.h"
#include "terminal.h"

#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#ifdef ESP32
#if IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif
#endif

#include <vector>


using namespace std;

static char TAG[] = "cyclic";

struct SubTask
{
	SubTask(const char *n, unsigned(*c)(void*), void *a, unsigned nr)
	: name(n)
	, code(c)
	, arg(a)
	, nextrun(nr)
	, cputime(0)
	, peaktime(0)
	, calls(0)
	{ }

	SubTask *next;
	const char *name;
	unsigned (*code)(void*);
	void *arg;
	uint64_t nextrun;
	long unsigned cputime;
	uint32_t peaktime;
	unsigned calls;
};


struct SubTaskCmp
{
	bool operator () (const SubTask &l, const SubTask &r) const
	{ return l.nextrun < r.nextrun; }
};


static SubTask *SubTasks;
static SemaphoreHandle_t Lock;


int cyclic_add_task(const char *name, unsigned (*loop)(void*), void *arg, unsigned initdelay)
{
	log_info(TAG,"add subtask %s",name);
	SubTask *n = new SubTask(name,loop,arg,esp_timer_get_time()+initdelay*1000);
	xSemaphoreTake(Lock,portMAX_DELAY);
	SubTask *s = SubTasks;
	while (s) {
		if (0 == strcmp(s->name,name)) {
			xSemaphoreGive(Lock);
			delete n;
			log_error(TAG,"subtask %s already exists",name);
			return 1;
		}
		s = s->next;
	}
	n->next = SubTasks;
	SubTasks = n;
	xSemaphoreGive(Lock);
	return 0;
}


int cyclic_rm_task(const char *name)
{
	xSemaphoreTake(Lock,portMAX_DELAY);
	SubTask *s = SubTasks, *p = 0;
	while (s) {
		if (!strcmp(name,s->name)) {
			if (p)
				p->next = s->next;
			else
				SubTasks = s->next;
			xSemaphoreGive(Lock);
			delete s;
			log_info(TAG,"removed subtask %s",name);
			return 0;
		}
		p = s;
		s = s->next;
	}
	xSemaphoreGive(Lock);
	return 1;
}


unsigned cyclic_execute()
{
	xSemaphoreTake(Lock,portMAX_DELAY);
	int64_t start = esp_timer_get_time();
	unsigned delay = 100;
	SubTask *t = SubTasks;
	while (t) {
		int32_t off = (int64_t)(t->nextrun - start);
		if (off <= 0) {
			unsigned d = t->code(t->arg);
			int64_t end = esp_timer_get_time();
			t->nextrun = end + (uint64_t)d * 1000LL;
			++t->calls;
			int64_t dt = end - start;
			t->cputime += dt;
			if (dt > t->peaktime)
				t->peaktime = dt;
			start = end;
			if (d < delay)
				delay = d;
		} else if (off/1000 < delay) {
			delay = off/1000;
		}
		t = t->next;
	}
	xSemaphoreGive(Lock);
	return delay;
}


void cyclic_setup()
{
	Lock = xSemaphoreCreateMutex();
	// cyclic_execute is called from the inted
}


int subtasks(Terminal &term, int argc, const char *args[])
{
	term.printf("%8s  %8s  %10s  %s\n","calls","peak","total","name");
	SubTask *s = SubTasks;
	while (s) {
		term.printf("%8u  %8u  %10lu  %s\n",s->calls,s->peaktime,s->cputime,s->name);
		s = s->next;
	}
	return 0;
}

