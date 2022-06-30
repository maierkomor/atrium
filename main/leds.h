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

#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ledmode {
	ledmode_auto = 0,
	ledmode_off,
	ledmode_on,
	ledmode_pulse_seldom,
	ledmode_pulse_often,
	ledmode_neg_seldom,
	ledmode_neg_often,
	ledmode_heartbeat,
	ledmode_slow,
	ledmode_medium,
	ledmode_fast,
	ledmode_once,
	ledmode_twice,
	ledmode_max	// not a mode, just for iteration
} ledmode_t;

#ifdef __cplusplus
extern "C" {
#endif
int leds_setup();
void statusled_set(ledmode_t);
uint16_t statusled_get();

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
}
#endif

#endif
