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


Action::Action(const char *n)
: name(n)
, ev(0)
, func(0)
{
//	log_dbug(TAG,"Action(%s)",n);
}


Action::Action(const char *n, void (*f)(void*),void *a, const char *t)
: name(n)
, text(t)
, ev(event_register("*trigger`",n))
, func(f)
, arg(a)
{
//	log_dbug(TAG,"Action(%s,...)",n);
	event_callback(ev,this);
}


void Action::activate(void *a)
{
	if (arg != 0)
		a = arg;
	uint64_t st = esp_timer_get_time();
	func(a);
	uint64_t end = esp_timer_get_time();
	++num;
	unsigned dt = end - st;
	sum += dt;
	if (dt < min)
		min = dt;
	if (dt > max)
		max = dt;
}


Action *action_get(const char *name)
{
	if (strchr(name,' '))  {
		log_warn(TAG,"invalid action in search: '%s'",name);
		return 0;
	}
	Action x(name);
	auto i = Actions.find(x);
	if (i == Actions.end())
		return 0;
	return (Action*)&(*i);
}


int action_exists(const char *name)
{
	return action_get(name) != 0;
}


Action *action_add(const char *name, void (*func)(void *), void *arg, const char *text)
{
	if (name == 0)
		return 0;
	if (action_get(name)) {
		log_warn(TAG,"duplicated action %s",name);
		return 0;
	}
	log_dbug(TAG,"add %s",name);
	return (Action*) &(*Actions.emplace(name,func,arg,text).first);
}


int action_activate(const char *name)
{
	Action x(name);
	set<Action,less<Action>>::iterator i = Actions.find(x);
	if (i == Actions.end()) {
		log_warn(TAG,"unknown action '%s'",name);
		return 1;
	}
	log_dbug(TAG,"triggered action %s",name);
	Action &a = const_cast<Action&>(*i);
	a.activate();
	return 0;
}


int action_activate_arg(const char *name, void *arg)
{
	Action x(name);
	set<Action,less<Action>>::iterator i = Actions.find(x);
	if (i == Actions.end()) {
		log_warn(TAG,"unknown action '%s'",name);
		if (arg)
			free(arg);
		return 1;
	}
	log_dbug(TAG,"triggered action %s",name);
	Action &a = const_cast<Action&>(*i);
	a.activate(arg);
	return 0;
}


int action_dispatch(const char *n, size_t l)
{
	if (l == 0)
		l = strlen(n);
	size_t nl = l;
	const char *e = strchr(n,' ');
	if (e)
		nl = e-n;
	char name[nl+1];		// temporary for search
	name[nl] = 0;
	memcpy(name,n,nl);
	Action *a = action_get(name);
	if (a == 0) {
		log_warn(TAG,"dispatch unknown '%s'",name);
		return 1;
	}
	log_dbug(TAG,"dispatch %s",name);
	char *arg = 0;
	if (e) {
		size_t al = l - nl;
		arg = (char *) malloc(al);
		--al;
		memcpy(arg,e+1,al);
		arg[al] = 0;
	}
	event_trigger_arg(a->ev,arg);
	return 0;
}


void action_iterate(void (*f)(void*,const Action *),void *p)
{
	for (const Action &a : Actions)
		f(p,&a);
	f(p,0);
}


static void action_event_cb(void *arg)
{
	Action *a = (Action *)arg;
	assert(a);
	log_dbug(TAG,"action %s",a->name);
	a->activate();
}


void action_setup()
{
	ActionTriggerEvt = event_register("action`trigger");
	Action *a = action_add("action!execute",action_event_cb,0,0);
	event_callback(ActionTriggerEvt,a);
}
