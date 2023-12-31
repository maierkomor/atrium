/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#include "stream.h"
#include "support.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


char *streol(char *b, size_t n)
{
	char *nl = (char*)memchr(b,'\n',n);
	char *cr = (char*)memchr(b,'\r',n);
	if (nl && cr) {
		if (nl < cr)
			return nl;
		return cr;
	}
	if (nl)
		return nl;
	return cr;
}


int parse_ipv4(uint32_t *ip, const char *str)
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


/* old version (double code size)
void ip4_to_ascii(stream &o, uint32_t ip)
{
	o	<< (ip & 0xff)
		<< '.'
		<< ((ip >> 8) & 0xff)
		<< '.'
		<< ((ip >> 16) & 0xff)
		<< '.'
		<< ((ip >> 24) & 0xff)
		;
}
*/


void id64_to_ascii(stream &o, uint64_t id)
{
	/*
	o.printf("%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x"
		, (unsigned) (id&0xff)
		, (unsigned) (id>>8)&0xff
		, (unsigned) (id>>16)&0xff
		, (unsigned) (id>>24)&0xff
		, (unsigned) (id>>32)&0xff
		, (unsigned) (id>>40)&0xff
		, (unsigned) (id>>48)&0xff
		, (unsigned) (id>>56)&0xff
		);
	*/
	o.printf("%02x",id&0xff);
	for (int i = 0; i < 7; ++i) {
		id >>= 8;
		o.printf(":%02x",id&0xff);
	}

}


void ip4_to_ascii(stream &o, uint32_t ip)
{
	o.printf("%d.%d.%d.%d"
		, (ip & 0xff)
		, ((ip >> 8) & 0xff)
		, ((ip >> 16) & 0xff)
		, ((ip >> 24) & 0xff)
		);
}


void min_of_day_to_ascii(stream &o, uint16_t m)
{
	o.printf("%02u:%02u",(m / 60),(m % 60));
}


void degC_to_ascii(stream &o, float f)
{
	char buf[16];
	float_to_str(buf,f);
	o << buf;
	o << " \u00b0C";
}


void humid_to_ascii(stream &o, float f)
{
	char buf[16];
	float_to_str(buf,f);
	o << buf;
	o << " %";
}


void press_to_ascii(stream &o, float f)
{
	char buf[16];
	float_to_str(buf,f);
	o << buf;
	o << " hPa";
}


