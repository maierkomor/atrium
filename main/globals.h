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

#ifndef GLOBALS_H
#define GLOBALS_H

#include <esp_timer.h>
#include <nvs.h>

extern nvs_handle NVS;


#ifdef __cplusplus
class NodeConfig;
class HardwareConfig;
class EnvObject;
class EnvString;
class EnvString;
class EnvNumber;

extern NodeConfig Config;
extern HardwareConfig HWConf;
extern EnvObject *RTData;
extern EnvString *UpdateState, *Localtime;

void globals_setup();
void rtd_lock();
void rtd_unlock();
void rtd_update();	// assumes rtd_lock()'ed
void runtimedata_to_json(class stream &json);
const char *localtimestr(char *s);
void get_time_of_day(uint8_t *h, uint8_t *m, uint8_t *s = 0, uint8_t *wd = 0, uint8_t *md = 0, uint8_t *mon = 0, unsigned *year = 0);
#endif

extern const char ResetReasons[][12];

// time in usec
typedef int64_t timestamp_t;
#define timestamp() esp_timer_get_time()

// time in msec
typedef uint32_t uptime_t;
#define uptime() ((uint32_t)(esp_timer_get_time()/1000))

#endif
