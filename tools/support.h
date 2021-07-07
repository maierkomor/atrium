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

template<typename T>
void id64_to_ascii(T &o, uint64_t id)
{
	char buf[32], *at = buf;
	at += sprintf(at,"%02x",(unsigned)id&0xff);
	for (int i = 0; i < 7; ++i) {
		id >>= 8;
		at += sprintf(at,":%02x",(unsigned)id&0xff);
	}
	o << buf;
}


template<typename T>
void ip4_to_ascii(T &o, uint32_t ip)
{
	char buf[16];
	sprintf(buf,"%d.%d.%d.%d"
		, (ip & 0xff)
		, ((ip >> 8) & 0xff)
		, ((ip >> 16) & 0xff)
		, ((ip >> 24) & 0xff)
		);
	o << buf;
}


template<typename T>
void min_of_day_to_ascii(T &o, uint16_t m)
{
	char buf[8];
	sprintf(buf,"%02u:%02u",(m / 60),(m % 60));
	o << buf;
}


inline int parse_ipv4(uint32_t *ip, const char *str)
{
	int v[4];
	int n;
	int r = sscanf(str,"%d.%d.%d.%d%n",v,v+1,v+2,v+3,&n);
	if (r != 4)
		return -1;
	if (v[0] & ~0xff)
		return -2;
	if (v[1] & ~0xff)
		return -2;
	if (v[2] & ~0xff)
		return -2;
	if (v[3] & ~0xff)
		return -2;
	*ip = (v[0]) | (v[1] << 8) | (v[2] << 16) | (v[3] << 24);
	return n;
}


#endif
