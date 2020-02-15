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

#ifndef TIMECTRL_H
#define TIMECTRL_H

#include <stdint.h>


typedef enum { Sun = 0, Mon, Tue, Wed, Thu, Fri, Sat, WD, WE, ED } weekdays_t;

extern const char *Weekdays_en[];
extern const char *Weekdays_de[];

struct Action
{
	const char *name;
	void (*func)();
	const char *text;	// descriptive help text
};

#ifdef __cplusplus
#include <vector>
extern std::vector<Action> Actions;


struct Alarm
{
	weekdays_t day;
	uint8_t hour, min;
	bool enabled;
	Action *action;
};


void add_action(const char *name, void (*func)(), const char *text = 0);
#endif

const char *clockstr(const char *sntpstr);
unsigned get_minute_of_day();

#ifdef __cplusplus
extern "C"
#endif
void actions_setup();

#endif
