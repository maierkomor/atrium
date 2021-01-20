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

#ifdef CONFIG_SUBTASKS
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


static vector<SubTask> SubTasks;
static SemaphoreHandle_t Lock;


int cyclic_add_task(const char *name, unsigned (*loop)(void*), void *arg, unsigned initdelay)
{
	log_info(TAG,"add subtask %s",name);
	xSemaphoreTake(Lock,portMAX_DELAY);
	for (const auto &s : SubTasks) {
		if (0 == strcmp(s.name,name)) {
			xSemaphoreGive(Lock);
			log_error(TAG,"subtask %s already exists",name);
			return 1;
		}
	}
	SubTasks.push_back(SubTask(name,loop,arg,esp_timer_get_time()+initdelay*1000));
	xSemaphoreGive(Lock);
	return 0;
}


int cyclic_rm_task(const char *name)
{
	xSemaphoreTake(Lock,portMAX_DELAY);
	for (auto i = SubTasks.begin(), e = SubTasks.end(); i != e; ++i) {
		if (!strcmp(name,i->name)) {
			SubTasks.erase(i);
			xSemaphoreGive(Lock);
			log_info(TAG,"removed subtask %s",name);
			return 0;
		}
	}
	xSemaphoreGive(Lock);
	return 1;
}


static void cyclic_tasks(void *)
{
	for (;;) {
		xSemaphoreTake(Lock,portMAX_DELAY);
		int64_t start = esp_timer_get_time();
		int32_t delay = 100;
		for (SubTask &t : SubTasks) {
			int32_t off = (int64_t)(t.nextrun - start);
			if (off < 0) {
				unsigned d = t.code(t.arg);
				int64_t end = esp_timer_get_time();
				t.nextrun = end + (uint64_t)d * 1000LL;
				++t.calls;
				int64_t dt = end - start;
				t.cputime += dt;
				if (dt > t.peaktime)
					t.peaktime = dt;
				start = end;
				if (d < delay)
					delay = d;
			} else if (off/1000 < delay) {
				delay = off/1000;
			}

		}
		xSemaphoreGive(Lock);
		vTaskDelay(delay ? delay/portTICK_PERIOD_MS : 1);
	}
}


void cyclic_setup()
{
	Lock = xSemaphoreCreateMutex();
	BaseType_t r = xTaskCreatePinnedToCore(&cyclic_tasks, TAG, 4096, (void*)0, 8, NULL, APP_CPU_NUM);
	if (r != pdPASS)
		log_error(TAG,"error creating subtask task: %s",esp_err_to_name(r));
}


int subtasks(Terminal &term, int argc, const char *args[])
{
	term.printf("%-16s  %8s  %8s  %10s\n","name","calls","peak","total");
	for (SubTask s : SubTasks) 
		term.printf("%-16s  %8u  %8u  %10lu\n",s.name,s.calls,s.peaktime,s.cputime);
	return 0;
}

#else	// no CONFIG_SUBTASKS

struct subtask_args
{
	unsigned initdelay;
	unsigned (*loopfnc)();
};

static void cyclic_task(void *param)
{
	struct subtask_args *st = (struct subtask_args*) param;
	if (st->initdelay)
		vTaskDelay(st->initdelay/portTICK_PERIOD_MS);
	unsigned (*func)() = (unsigned (*)())st->loopfnc;
	free(param);
	for (;;) {
		unsigned d = func();
		if (d == 0)
			vTaskDelete(0);
		vTaskDelay(d/portTICK_PERIOD_MS);
	}
}


int cyclic_add_task(const char *name, unsigned (*loop_fnc)(void), unsigned initdelay)
{
	struct subtask_args *st = (struct subtask_args*) malloc(sizeof(struct subtask_args));
	st->initdelay = initdelay;
	st->loopfnc = loop_fnc;
	BaseType_t r = xTaskCreatePinnedToCore(&cyclic_task, name, 4096, (void*)st, 5, NULL, 1);
	if (r != pdPASS) {
		log_error(TAG,"error creating task %s: %s",name,esp_err_to_name(r));
		return r;
	}
	return 0;
}


/*
void cyclic_rm_task(const char *name)
{
	// not supported right now!
	abort();
}
*/

#endif // CONFIG_SUBTASKS


