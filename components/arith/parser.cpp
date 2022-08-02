/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#include "parser.h"
#include <math.h>
#include <string.h>

struct NamedFn1
{
	const char *n;
	fn1arg_t fn;
};


struct NamedFn2
{
	const char *n;
	fn2arg_t fn;
};


static NamedFn1 Fn1Arg[] = {
	{ "sin",sinf },
	{ "cos",cosf },
	{ "sqrt",sqrtf },
};

static NamedFn2 Fn2Arg[] = {
	{ "min", fminf },
	{ "max", fmaxf },
};


fn1arg_t fn1arg_get(const char *n)
{
	for (auto &x : Fn1Arg) {
		if (0 == strcmp(n,x.n))
			return x.fn;
	}
	return 0;
}


fn2arg_t fn2arg_get(const char *n)
{
	for (auto &x : Fn2Arg) {
		if (0 == strcmp(n,x.n))
			return x.fn;
	}
	return 0;

}
