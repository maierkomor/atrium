/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#ifndef MQTT_H
#define MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_setup(void);
void mqtt_start(void* = 0);
void mqtt_stop(void* = 0);
int mqtt_pub(const char *t, const char *v, int len, int retain, int qos);
int mqtt_pub_nl(const char *t, const char *v, int len, int retain, int qos);
int mqtt_sub(const char *topic, void (*callback)(const char *,const void*,size_t));
void mqtt_set_dmesg(const char *m, size_t s);

#ifdef __cplusplus
}

const char *mqtt(class Terminal &term, int argc, const char *args[]);
#endif

#endif
