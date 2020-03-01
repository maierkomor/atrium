/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#ifndef WIFI_H
#define WIFI_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define WIFI_UP 1
#define STATION_UP 2
#define SOFTAP_UP 4

extern EventGroupHandle_t WifiEvents;
typedef enum { station_stopped, station_starting, station_connected, station_disconnected } sta_mode_t;
extern sta_mode_t StationMode;

#ifdef __cplusplus
extern "C" {
#endif
bool wifi_setup();
uint32_t resolve_hostname(const char *h);
void wifi_wait();
void wifi_wait_station();
bool wifi_start_station(const char *ssid, const char *pass);
bool wifi_stop_station();
bool wifi_start_softap(const char *ssid, const char *pass);
bool wifi_stop_softap();
bool wifi_station_isup();
bool wifi_softap_isup();
bool eth_isup();
void wps_start();
int smartconfig_start();
void smartconfig_stop();
bool smartconfig_running();
#ifdef __cplusplus
}
#endif

#endif
