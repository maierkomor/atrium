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

#include "strstream.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include <assert.h>

using namespace std;


strstream::~strstream()
{

}


int strstream::put(char c)
{
	m_str += c;
	return 1;
}


int strstream::printf(const char *f, ...)
{
	va_list val;

	va_start(val,f);
	char *str;
	int n = vasprintf(&str,f,val);
	va_end(val);
	write(str,n);
	free(str);
	return n;
}


int strstream::write(const char *s, size_t l)
{
	int n = l;
	const char *nl = (const char *)memchr(s,'\n',l);
	while (nl) {
		m_str.append(s,nl-s);
		m_str += '\r';
		m_str += '\n';
		++nl;
		++n;
		l -= (nl-s);
		s = nl;
		nl = (const char *)memchr(s,'\n',l);
	}
	m_str.append(s,l);
	return n;
}

