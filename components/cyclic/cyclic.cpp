/*
 *  Copyright (C) 2017-2022, Thomas Maier-Komor
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
//#include <driver/gpio.h>

#include <vector>

#if!defined APP_CPU_NUM || defined CONFIG_FREERTOS_UNICORE
#define CYCLIC_CPU_NUM 0
#else
#define CYCLIC_CPU_NUM APP_CPU_NUM
#endif

#ifndef CONFIG_CYCLIC_STACK_SIZE
#define CONFIG_CYCLIC_STACK_SIZE 4096
#endif

using namespace std;

#define TAG MODULE_CYCLIC
#define STATIC_TASK

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

#ifdef STATIC_TASK
static StackType_t CyclicStack[CONFIG_CYCLIC_STACK_SIZE];
static StaticTask_t CyclicTask;
#endif

static SubTask *SubTasks = 0;
static SemaphoreHandle_t Mtx = 0;
static volatile uint64_t TimeSpent = 0;

#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
#define busy_set(...)
#elif defined CONFIG_LEDS
extern "C" void busy_set(int on);
#else
#define busy_set(...)
#endif


int cyclic_add_task(const char *name, unsigned (*loop)(void*), void *arg, unsigned initdelay)
{
	log_info(TAG,"add subtask %s",name);
	SubTask *n = new SubTask(name,loop,arg,esp_timer_get_time()+initdelay*1000);
	Lock lock(Mtx);
	SubTask *s = SubTasks;
	while (s) {
		if (0 == strcmp(s->name,name)) {
			xSemaphoreGive(Mtx);
			delete n;
			log_error(TAG,"subtask %s already exists",name);
			return 1;
		}
		s = s->next;
	}
	n->next = SubTasks;
	SubTasks = n;
	return 0;
}


int cyclic_rm_task(const char *name)
{
	unsigned r = 1;
	MLock lock(Mtx);
	SubTask *s = SubTasks, *p = 0;
	while (s) {
		if (!strcmp(name,s->name)) {
			if (p)
				p->next = s->next;
			else
				SubTasks = s->next;
			lock.unlock();
			delete s;
			log_info(TAG,"removed subtask %s",name);
			r = 0;
			break;
		}
		p = s;
		s = s->next;
	}
	return r;
}


unsigned cyclic_execute()
{
	MLock lock(Mtx,__FUNCTION__);
	int64_t start = esp_timer_get_time();
	int64_t begin = start;
	unsigned delay = 100;
	SubTask *t = SubTasks;
	while (t) {
		int32_t off = (int64_t)(t->nextrun - start);
		if (off <= 0) {
			lock.unlock();
//			con_printf("subtask => %s\n",t->name);
			busy_set(true);
			unsigned d = t->code(t->arg);
			busy_set(false);
			int64_t end = esp_timer_get_time();
			lock.lock();
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
	int64_t end = esp_timer_get_time();
	TimeSpent += end-begin;
	return delay;
}


#ifdef ESP32
static void cyclic_task(void *)
{
	for (;;) {
		unsigned d = cyclic_execute();
		vTaskDelay(d / portTICK_PERIOD_MS);
	}
}
#endif


void cyclic_setup()
{
	Mtx = xSemaphoreCreateMutex();
#ifdef ESP32
#ifdef STATIC_TASK
	xTaskCreateStaticPinnedToCore(cyclic_task, "cyclic", sizeof(CyclicStack), (void*)0, 20, CyclicStack, &CyclicTask, CYCLIC_CPU_NUM);
#else
	BaseType_t r = xTaskCreatePinnedToCore(cyclic_task, "cyclic", 8192, (void*)0, 20, NULL, 1);
	if (r != pdPASS)
		log_error(TAG,"create task: %d",r);
#endif // STATIC_TASK
#else
	// cyclic_execute is called from the event task
#endif
}


int subtasks(Terminal &term, int argc, const char *args[])
{
	term.printf("%8s  %9s  %7s  %s\n","calls","peak","total","name");
	SubTask *s = SubTasks;
	const char *p = "num kM";
	while (s) {
		auto pt = s->peaktime;
		auto ct = s->cputime;
		uint8_t pd = 0, cd = 0;
		while (ct > 30000) {
			ct /= 1000;
			++cd;
		}
		while (pt > 30000) {
			pt /= 1000;
			++pd;
		}
		term.printf("%8u  %8u%c  %6lu%c  %s\n",s->calls,pt,p[pd],ct,p[cd],s->name);
		s = s->next;
	}
	MLock lock(Mtx,__FUNCTION__);
	auto tt = TimeSpent;
	lock.unlock();
	uint8_t d = 0;
	while (tt > 30000) {
		tt /= 1000;
		++d;
	}
	term.printf("total %u%cs\n",(unsigned)tt,p[d]);
	return 0;
}

