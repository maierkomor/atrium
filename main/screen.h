/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include "estring.h"


typedef enum clockmode {
	cm_version,
	cm_time,
	cm_date,
	cm_stopwatch,
	cm_lua,
	CLOCK_MODE_MAX,
} clockmode_t;

struct Screen
{
	class TextDisplay *disp;
	uint32_t sw_start = 0, sw_delta = 0, sw_pause = 0, modestart = 0;
	uint8_t display[8];
	uint8_t digits;
	clockmode_t mode = cm_time;
	bool modech = false;
	estring prev;

	void display_env();
	void display_time();
	void display_date();
	void display_data();
	void display_sw();
	void display_version();
};

#endif
