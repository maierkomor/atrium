/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

#include "termstream.h"
#include "terminal.h"
#include "log.h"

#include <stdio.h>


TermStream &TermStream::operator << (unsigned short v)
{
	char buf[8];
	int n = snprintf(buf,sizeof(buf),"%hu",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (signed short v)
{
	char buf[8];
	int n = snprintf(buf,sizeof(buf),"%hd",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (unsigned v)
{
	char buf[16];
	int n = snprintf(buf,sizeof(buf),"%u",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (signed v)
{
	char buf[16];
	int n = snprintf(buf,sizeof(buf),"%d",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (unsigned long v)
{
	char buf[32];
	int n = snprintf(buf,sizeof(buf),"%lu",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (signed long v)
{
	char buf[32];
	int n = snprintf(buf,sizeof(buf),"%ld",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (unsigned long long v)
{
	char buf[32];
	int n = snprintf(buf,sizeof(buf),"%llu",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (signed long long v)
{
	char buf[32];
	int n = snprintf(buf,sizeof(buf),"%lld",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (double v)
{
	char buf[32];
	int n = snprintf(buf,sizeof(buf),"%G",v);
	term.print(buf,n);
	return *this;
}


TermStream &TermStream::operator << (const char *s)
{
	term.print(s);
	return *this;
}


TermStream &TermStream::operator << (signed char c)
{
	term.print((const char *)&c,1);
	return *this;
}


TermStream &TermStream::operator << (unsigned char c)
{
	term.print((const char *)&c,1);
	return *this;
}


TermStream &TermStream::operator << (char c)
{
	term.print((const char *)&c,1);
	return *this;
}



void TermStream::put(char c)
{
	term.print(&c,1);
}



int TermStream::printf(const char *f, ...)
{
	va_list val;
	va_start(val,f);
	term.vprintf(f,val);
	va_end(val);
	return 0;
}


void TermStream::write(const char *s, size_t n)
{
	term.print(s,n);
}


