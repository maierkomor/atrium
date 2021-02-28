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

class Terminal;

int update_setting(Terminal &t, const char *name, const char *value);
void list_settings(Terminal &t);

extern "C" {
#endif

void cfg_activate_actions();
void cfg_activate_triggers();
int cfg_backup_create();
int cfg_backup_restore();
void cfg_init_functions();
int cfg_get_uvalue(const char *name, unsigned *u, unsigned def = 0);
int cfg_get_dvalue(const char *name, signed *u, signed def = 0);
int cfg_get_fvalue(const char *name, double *u, double def = 0);
void cfg_set_uvalue(const char *name, unsigned u);
void cfg_set_dvalue(const char *name, signed u);
void cfg_set_fvalue(const char *name, double u);
extern const char Version[];
bool verifyPassword(const char *p);
bool isValidBaudrate(long l);
void cfg_activate();
void cfg_set_station(const uint8_t *ssid, const uint8_t *pass);
const char *cfg_get_domainname();
void initDns();
int readNVconfig(const char *, uint8_t **, size_t *len);
//int readSettings();
int setPassword(const char *p);
int cfg_read_nodecfg();
int cfg_read_hwcfg();
void startWPS();
void cfg_init_defaults();
int cfg_store_hwcfg();
int cfg_store_nodecfg();
void sntp_start();
void cfg_factory_reset(void * = 0);
int cfg_set_hostname(const char *hn);
void cfg_clear_nodecfg();
int cfg_erase_nvs();
void nvs_setup();
int set_cpu_freq(unsigned mhz);
int set_timezone(const char *v);
void store_nvs_u8(const char *, uint8_t);
uint8_t read_nvs_u8(const char *, uint8_t);
int writeNVM(const char *name, const uint8_t *buf, size_t s);


#ifdef __cplusplus
}
#endif


#endif
