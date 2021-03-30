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

#ifndef ACTIONS_H
#define ACTIONS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus

class Action
{
	public:
	const char *name;
	const char *text = 0;	// descriptive help text
	uint32_t min = UINT32_MAX, max = 0, sum = 0, num = 0;

	Action(const char *n, void (*f)(void*),void *a, const char *t);
	Action(const char *n)
	: name(n)
	, func(0)
	{ }

	void activate();

	private:
	void (*func)(void *);
	void *arg = 0;
};

extern "C" {
#endif

Action *action_add(const char *name, void (*func)(void *), void *arg, const char *text);
const char *action_get_name(size_t);
const char *action_get_text(size_t);
const char *action_text(const char *name);
Action *action_get(const char *name);
int action_activate(const char *name);
int action_exists(const char *name);
void action_iterate(void (*)(void*,const Action *),void *);
void actions_setup();

#ifdef __cplusplus
}
#endif

#endif