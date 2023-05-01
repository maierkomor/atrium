/*
 *  Copyright (C) 2017-2023, Thomas Maier-Komor
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
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#if defined CONFIG_IDF_TARGET_ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif

#include <set>

using namespace std;

#define TAG MODULE_ACTION

bool operator < (const Action &l, const Action &r)
{ return strcmp(l.name,r.name) < 0; }

bool operator < (const Action &l, const char *r)
{ return strcmp(l.name,r) < 0; }

static set<Action,less<Action>> Actions;
static event_t ActionTriggerEvt = 0;


#if 0	// non-debugging version inlined
Action::Action(const char *n)
: name(n)
, func(0)
{
	log_dbug(TAG,"Action(%s)",n);
}


Action::Action(const char *n, void (*f)(void*),void *a, const char *t)
: name(n)
, text(t)
, func(f)
, arg(a)
{
	log_dbug(TAG,"Action(%s,...)",n);
}
#endif

void Action::activate(void *a)
{
	if (arg != 0)
		a = arg;
	assert(func);
	uint64_t st = esp_timer_get_time();
	func(a);
	uint64_t end = esp_timer_get_time();
	++num;
	unsigned dt = end - st;
	sum += dt;
	if (dt < min)
		min = dt;
	else if (dt > max)
		max = dt;
}


Action *action_get(const char *name)
{
	Action x(name);
	if (const char *sp = strchr(name,' '))  {
		size_t l = sp-name;
		char *tmp = (char *)alloca(l+1);
		memcpy(tmp,name,l);
		tmp[l] = 0;
		x.name = tmp;
	}
	auto i = Actions.find(x);
	if (i != Actions.end())
		return (Action*)&(*i);
	// no warning here, because it is used to add new actions
	return 0;
}


/*
int action_exists(const char *name)
{
	if (action_get(name) == 0) {
		log_warn(TAG,"action %s does not exist",name);
		return 0;
	}
	return 1;
}
*/


Action *action_add(const char *name, void (*func)(void *), void *arg, const char *text)
{
	if (name) {
		if (0 == action_get(name)) {
			log_dbug(TAG,"add %s",name);
			return (Action*) &(*Actions.emplace(name,func,arg,text).first);
		}
		log_warn(TAG,"action exists: %s",name);
	}
	return 0;
}


int action_activate(const char *name)
{
	if (Action *a = action_get(name)) {
		log_dbug(TAG,"trigger %s",name);
		a->activate();
		return 0;
	}
	return 1;
}


int action_activate_arg(const char *name, void *arg)
{
	if (Action *a = action_get(name)) {
		log_dbug(TAG,"trigger %s(%p)",name,arg);
		a->activate(arg);
		return 0;
	}
	return 1;
}


void action_dispatch(const char *name, size_t l)
{
	if (l == 0)
		l = strlen(name);
	char *arg = (char *) malloc(l+1);
	arg[l] = 0;
	memcpy(arg,name,l);
	event_trigger_arg(ActionTriggerEvt,arg);
}


void action_iterate(void (*f)(void*,const Action *),void *p)
{
	for (const Action &a : Actions)
		f(p,&a);
}


static void action_event_cb(void *arg)
{
	const char *as = (const char *)arg;
	size_t al = strlen(as);
	char tmp[al+1];
	memcpy(tmp,as,sizeof(tmp));
	char *sp = strchr(tmp,' ');
	if (sp) {
		*sp = 0;
		++sp;
	}
	if (Action *a = action_get(tmp)) {
		log_dbug(TAG,"action %s (%s)",a->name,sp?sp:"");
		a->activate(sp);
	} else {
		log_warn(TAG,"unknown action %s",tmp);
	}
}


void action_setup()
{
	ActionTriggerEvt = event_register("action`trigger");
	Action *a = action_add("action!execute",action_event_cb,0,0);
	event_callback(ActionTriggerEvt,a);
}
