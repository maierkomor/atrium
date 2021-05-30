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

#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>
#include <vector>

typedef unsigned event_t;

#ifdef __cplusplus
class Action;

int event_callback(event_t e, Action *a);
int event_detach(event_t e, Action *a);
const std::vector<Action *> &event_callbacks(event_t);
event_t event_register(const char *cat, const char *type = 0);
extern "C" {
#endif // __cplusplus

int event_callback(const char *e, const char *a);
int event_detach(const char *e, const char *a);
void event_trigger(event_t e);
void event_trigger_nd(event_t id);	// no-debug version for syslog only
void event_trigger_arg(event_t e, void *);
void event_isr_trigger(event_t e);
event_t event_id(const char *n);
const char *event_name(event_t);
uint32_t event_occur(event_t);
uint64_t event_time(event_t);
char *concat(const char *s0, const char *s1);

#ifdef __cplusplus
}
#endif

#endif
