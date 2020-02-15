/*
 *  Copyright (C) 2018-2019, Thomas Maier-Komor
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

#ifndef ALIVE_H
#define ALIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ledmode {
	ledoff = 0,
	ledon,
	pulse_seldom,
	pulse_often,
	neg_seldom,
	neg_often,
	heartbeat,

	ledmode_max	// not a mode, just for iteration
} ledmode_t;

#ifdef __cplusplus
extern "C"
#endif
void set_aliveled(uint16_t);

uint16_t get_aliveled();

#ifdef __cplusplus
extern "C"
#endif
void alive_task(void *ignored);

#ifdef __cplusplus
}
#endif

#endif
