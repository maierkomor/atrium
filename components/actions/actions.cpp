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

#include "actions.h"
#include "log.h"

#include <assert.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#ifdef ESP32
#include <rom/gpio.h>
#endif

#include <set>

using namespace std;


struct AName
{
	const char *name;
};

bool operator < (const Action &l, const Action &r)
{ return strcmp(l.name,r.name) < 0; }

bool operator < (const Action &l, const AName &r)
{ return strcmp(l.name,r.name) < 0; }

bool operator < (const AName &l, const Action &r)
{ return strcmp(l.name,r.name) < 0; }

static set<Action,less<Action>> Actions;

static char TAG[] = "action";


Action::Action(const char *n, void (*f)(void*),void *a, const char *t)
: name(n)
, func(f)
, arg(a)
, text(t)
{

}


Action *action_get(const char *name)
{
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
	if (action_get(name)) {
		log_error(TAG,"action %s already exists",name);
		return 0;
	}
	log_dbug(TAG,"add %s",name);
	return (Action*) &(*Actions.emplace(name,func,arg,text).first);
}


int action_activate(const char *name)
{
	Action x(name);
	auto i = Actions.find(x);
	if (i == Actions.end()) {
		log_warn(TAG,"unable to execute unknown action '%s'",name);
		return 1;
	}
	log_dbug(TAG,"triggered action %s",name);
	i->func(i->arg);
	return 0;
}


void action_iterate(void (*f)(void*,const Action *),void *p)
{
	for (const Action &a : Actions)
		f(p,&a);
	f(p,0);
}
