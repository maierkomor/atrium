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

#ifndef GLOBALS_H
#define GLOBALS_H

#include <esp_timer.h>
#ifdef __cplusplus
#include "binformats.h"
#endif


#ifdef __cplusplus
extern NodeConfig Config;
extern RunTimeData RTData;
extern bool StationUp, DhtDebug;

void runtimedata_to_json(class stream &json);
void get_time_of_day(uint8_t *h, uint8_t *m, uint8_t *s = 0, uint8_t *wd = 0, uint8_t *md = 0, uint8_t *mon = 0, unsigned *year = 0);
#endif


typedef int64_t timestamp_t;
#define timestamp() esp_timer_get_time()

typedef uint32_t uptime_t;
#define uptime() ((uint32_t)(esp_timer_get_time()/1000))

#endif
