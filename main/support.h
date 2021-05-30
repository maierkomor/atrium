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

#ifndef SUPPORT_H
#define SUPPORT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define null_indent(a,b)

#ifdef __cplusplus
class stream;

struct CStrLess
{
	bool operator () (const char *l, const char *r)
	{
		return strcmp(l,r) < 0;
	}
};

struct SubstrLess
{
	bool operator () (const char *l, const char *r)
	{
		size_t ll = strlen(l);
		size_t rl = strlen(r);
		return memcmp(l,r,ll < rl ? ll : rl) < 0;
	}
};

void id64_to_ascii(stream &o, uint64_t id);
void ip4_to_ascii(stream &o, uint32_t ip);
void degC_to_ascii(stream &o, float f);
void humid_to_ascii(stream &o, float f);
void press_to_ascii(stream &o, float f);
void min_of_day_to_ascii(stream &o, uint16_t m);


extern "C" {
#endif

char *streol(char *b, size_t n);
int parse_ipv4(uint32_t *ip, const char *str);


#ifdef __cplusplus
}
#endif

#endif
