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

#ifndef SETTING_H
#define SETTING_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#ifdef __cplusplus
#include <utility>

extern EventGroupHandle_t SysEvents;
extern std::pair<const char *, int (*)(const char *)> SetFns[];

extern "C" {
#endif

extern const char Version[];
int setPassword(const char *p);
bool verifyPassword(const char *p);
bool isValidBaudrate(long l);
void activateSettings();
void initDns();
uint8_t *readNVconfig(size_t *len);
bool readSettings();
void setupDefaults();
void storeSettings();
void factoryReset();
int setHostname(const char *hn);
void clearSettings();
void eraseSettings();
void nvs_setup();
void settings_setup();
int change_setting(const char *name, const char *value);
int set_cpu_freq(unsigned mhz);
int set_timezone(const char *v);

#ifdef __cplusplus
}
#endif


#endif
