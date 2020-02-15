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


strstream &strstream::operator << (char c)
{
	m_str += c;
	return *this;
}


strstream &strstream::operator << (unsigned u)
{
	char buf[64];
	int n = snprintf(buf,sizeof(buf),"%u",u);
	m_str.append(buf,n);
	return *this;
}


strstream &strstream::operator << (signed u)
{
	char buf[64];
	int n = snprintf(buf,sizeof(buf),"%d",u);
	m_str.append(buf,n);
	return *this;
}


strstream &strstream::operator << (double d)
{
	char buf[64];
	int n = snprintf(buf,sizeof(buf),"%G",d);
	m_str.append(buf,n);
	return *this;
}


strstream &strstream::operator << (const char *s)
{
	write(s,strlen(s));
	return *this;
}


void strstream::put(char c)
{
	m_str += c;
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


void strstream::write(const char *s, size_t l)
{
	const char *nl = (const char *)memchr(s,'\n',l);
	while (nl) {
		m_str.append(s,nl-s);
		m_str += '\r';
		m_str += '\n';
		++nl;
		l -= (nl-s);
		s = nl;
		nl = (const char *)memchr(s,'\n',l);
	}
	m_str.append(s,l);
}


