/*
 *  Copyright (C) 2020-2022, Thomas Maier-Komor
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

#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>

typedef uint16_t event_t;
typedef uint32_t trigger_t;


#ifdef __cplusplus
#include <vector>

class Action;

struct Callback
{
	Callback()
	{ }

	Callback(Action *a, char *v = 0, bool e = true)
	: action(a), arg(v), enabled(e)
	{ }

	Action *action = 0;
	char *arg = 0;
	bool enabled = true;
};

struct EventHandler
{
	explicit EventHandler(const char *n)
	: name(n)
	{ } 

	const char *name;	// name of event
	uint32_t occur = 0;
	uint64_t time = 0;
	std::vector<Callback> callbacks;
};


trigger_t event_callback(event_t e, Action *a);
// strdup's arg
trigger_t event_callback_arg(event_t e, Action *a, const char *arg);
int event_cb_set_en(event_t e, Action *a, bool en);
int event_cba_set_en(event_t e, Action *a, const char *, bool en);
int event_detach(event_t e, Action *a);
event_t event_register(const char *cat, const char *type = 0);
const EventHandler *event_handler(event_t);
extern "C" {
#else
typedef unsigned char bool;
#endif // __cplusplus

trigger_t event_callback(const char *e, const char *a);
// strdup's arg
trigger_t event_callback_arg(const char *e, const char *a, const char *arg);
int event_detach(const char *e, const char *a);
int event_start(void);
void event_init(void);
int event_trigger_en(trigger_t t, bool en);
void event_trigger(event_t e);
void event_trigger_nd(event_t id);	// no-debug version for syslog only

// Argument must be malloc()'ed by the caller.
// It will be free()'ed by the event infrastructure, because their might
// be != 1 event consumers...
void event_trigger_arg(event_t e, void *);
void event_isr_trigger(event_t e);
void event_isr_trigger_arg(event_t e, void *);
event_t event_id(const char *n);
const char *event_name(event_t);
uint32_t event_occur(event_t);
uint64_t event_time(event_t);
char *concat(const char *s0, const char *s1);

#ifdef __cplusplus
}
#endif

#endif
