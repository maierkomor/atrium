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

#ifndef TIMER_H
#define TIMER_H

#include "event.h"

typedef uint16_t timefuse_t;

#ifdef __cplusplus
timefuse_t timefuse_create(const char *name, unsigned itv, bool repeat = false);
int timefuse_start(const char *name);
int timefuse_stop(const char *name);
int timefuse_interval_set(const char *,unsigned);
unsigned timefuse_interval_get(const char *);
event_t timefuse_start_event(const char *);
event_t timefuse_stop_event(const char *);
event_t timefuse_timeout_event(const char *);
int timefuse_repeat_get(const char *);			// ESP32 only
int timefuse_repeat_set(const char *,bool);		// ESP32 only
//int timefuse_delete(const char *name);	-- not supported

extern "C" {
#endif
timefuse_t timefuse_get(const char *name);
const char *timefuse_name(timefuse_t);
//int timefuse_delete(timefuse_t);		-- not supported
int timefuse_start(timefuse_t);
int timefuse_stop(timefuse_t);
int timefuse_active(timefuse_t);
unsigned timefuse_interval_get(timefuse_t);
int timefuse_interval_set(timefuse_t,unsigned);
event_t timefuse_start_event(timefuse_t);
event_t timefuse_stop_event(timefuse_t);
event_t timefuse_timeout_event(timefuse_t);
timefuse_t timefuse_iterator();
timefuse_t timefuse_next(timefuse_t);
int timefuse_repeat_get(timefuse_t);			// ESP32 only
int timefuse_repeat_set(timefuse_t,bool);		// ESP32 only
#ifdef __cplusplus
}	// extern "C"
#endif

#endif
