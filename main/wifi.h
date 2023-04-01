/*
 *  Copyright (C) 2018-2021, Thomas Maier-Komor
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

#include "event.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

extern EventGroupHandle_t WifiEvents;
extern event_t StationDownEv, StationUpEv;

#ifdef __cplusplus
extern "C" {
#endif
int wifi_setup();
bool wifi_start_station(const char *ssid, const char *pass);
bool wifi_stop_station();
bool wifi_start_softap(const char *ssid, const char *pass);
bool wifi_stop_softap();
bool wifi_station_isup();
bool wifi_softap_isup();
bool eth_isup();
void wifi_wps_start(void* = 0);
int smartconfig_start();
void smartconfig_stop();
bool smartconfig_running();
#ifdef __cplusplus
}
#endif

#endif
