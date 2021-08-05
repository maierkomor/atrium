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


#include "stream.h"
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <math.h>


char *float_to_str(char *buf, float f)
{
	if (isnan(f)) {
		memcpy(buf,"NaN",4);
		return buf+3;
	}
	char *o = buf;
	if (f < 0) {
		*o++ = '-';
		f = -f;
	}
	int32_t a = truncf(f);
	f -= a;
	f *= 10;
	uint32_t b = roundf(f);
	if (b > 9) {
		b = 0;
		++a;
	}
	int n = sprintf(o,"%u.%u",a,b);
	o += n;		// don't forget the potential minus
	return o;
}


size_t chrcnt(const char *s, char c)
{
	size_t r = 0;
	while (const char *f = strchr(s,c)) {
		++r;
		s = f+1;
	}
	return r;
}


size_t chrcntn(const char *s, char c, size_t n)
{
	size_t r = 0;
	while (const char *f = (const char *)memchr(s,c,n)) {
		++r;
		++f;
		n -= (f-s);
		s = f;
	}
	return r;
}


int stream::print(const char *buf, size_t s)
{
	if (s == 0) {
		s = strlen(buf);
		if (s == 0)
			return 0;
	}
	int r, n = 0;
	if (m_crnl) {
		const char *at = buf;
		const char *nl = (const char*) memchr(buf,'\n',s);
		while (nl) {
			r = write(at,nl-at);
			if (r < 0)
				return -1;
			n += r;
			r = write("\r\n",2);
			if (r < 0)
				return -1;
			n += 2;
			s -= (nl-at)+1;
			at = nl + 1;
			nl = (const char *) memchr(at,'\n',s);
		}
		buf = at;
	}
	r = write(buf,s);
	if (r < 0)
		return -1;
	n += r;
	return n;
}


int stream::vprintf(const char *fmt, va_list v)
{
	char buf[120], *b;
	int n = vsnprintf(buf,sizeof(buf),fmt,v);
	if (n <= sizeof(buf)) {
		b = buf;
	} else {
		b = (char *) malloc(n+1);
		if (b == 0) {
			errno = ENOMEM;
			return -1;
		}
		n = vsprintf(b,fmt,v);
	}
	int r = print(b,n);
	if (b != buf)
		free(b);
	return r;
}


void stream::println(const char *s)
{
	write(s,strlen(s));
	println();
}


void stream::println()
{
	if (m_crnl)
		write("\r\n",2);
	else
		write("\n",1);
}


int stream::printf(const char *f, ...)
{
#if 1
	va_list val;
	va_start(val,f);
	int n = vprintf(f,val);
	va_end(val);
	return n;
#else
	char buf[120], *b = buf;
	va_list val;
	va_start(val,f);
	int n = vsnprintf(buf,sizeof(buf),f,val);
	if (n >= sizeof(buf)) {
		n = vasprintf(&b,f,val);
		if (b == 0) {
			errno = ENOMEM;
			return -1;
		}
	}
	va_end(val);
	int r = print(b,n);
	if (buf != b)
		free(b);
	return r;
#endif
}


stream &stream::operator << (bool b)
{
	write(b ? "true" : "false", b ? 4 : 5);
	return *this;
}


static inline int print_u64(char *buf, size_t n, uint64_t x)
{
	char *at = buf, *e = buf+n;
	uint64_t y = x;
	uint64_t d = 1;
	while (x >= 10) {
		d *= 10;
		x /= 10;
	}
	do {
		unsigned c = y/d;
		y %= d;
		if (at < e)
			*at++ = c + '0';
		d /= 10;
	} while (d);
	if (at < e)
		*at = 0;
	return at-buf;
}


static inline int print_i64(char *buf, size_t n, int64_t v)
{
	if (v < 0) {
		if (n)
			*buf++ = '-';
		--n;
		return print_u64(buf,n,-v) + 1;
	} else {
		return print_u64(buf,n,v);
	}
}


stream &stream::operator << (uint64_t u)
{
	char buf[24];
	int n = print_u64(buf,sizeof(buf),u);
	write(buf,n);
	return *this;
}


stream &stream::operator << (int64_t i)
{
	char buf[24];
	int n = print_i64(buf,sizeof(buf),i);
	write(buf,n);
	return *this;
}

/*
stream &stream::operator << (unsigned short u)
{
	char buf[8];
	int n = snprintf(buf,sizeof(buf),"%hu",u);
	write(buf,n);
	return *this;
}


stream &stream::operator << (signed short u)
{
	char buf[8];
	int n = snprintf(buf,sizeof(buf),"%hd",u);
	write(buf,n);
	return *this;
}


stream &stream::operator << (unsigned u)
{
	char buf[16];
	int n = snprintf(buf,sizeof(buf),"%u",u);
	write(buf,n);
	return *this;
}


stream &stream::operator << (signed u)
{
	char buf[16];
	int n = snprintf(buf,sizeof(buf),"%d",u);
	write(buf,n);
	return *this;
}


stream &stream::operator << (unsigned long u)
{
	char buf[32];
	int n = snprintf(buf,sizeof(buf),"%lu",u);
	write(buf,n);
	return *this;
}


stream &stream::operator << (signed long u)
{
	char buf[32];
	int n = snprintf(buf,sizeof(buf),"%ld",u);
	write(buf,n);
	return *this;
}

#ifdef CONFIG_IDF_TARGET_ESP8266
static int print_u64(char *buf, size_t n, uint64_t x)
{
	char *at = buf, *e = buf+n;
	uint64_t y = x;
	uint64_t d = 1;
	while (x >= 10) {
		d *= 10;
		x /= 10;
	}
	do {
		unsigned c = y/d;
		y %= d;
		if (at < e)
			*at++ = c + '0';
		d /= 10;
	} while (d);
	if (at < e)
		*at = 0;
	return at-buf;
}


static int print_i64(char *buf, size_t n, int64_t v)
{
	if (v < 0) {
		if (n)
			*buf++ = '-';
		--n;
		return print_u64(buf,n,-v) + 1;
	} else {
		return print_u64(buf,n,v);
	}
}
#endif


stream &stream::operator << (unsigned long long u)
{
	char buf[32];
#ifdef CONFIG_IDF_TARGET_ESP8266
	int n = print_u64(buf,sizeof(buf),u);
#else
	int n = snprintf(buf,sizeof(buf),"%llu",u);
#endif
	write(buf,n);
	return *this;
}


stream &stream::operator << (signed long long u)
{
	char buf[32];
#ifdef CONFIG_IDF_TARGET_ESP8266
	int n = print_i64(buf,sizeof(buf),u);
#else
	int n = snprintf(buf,sizeof(buf),"%lld",u);
#endif
	write(buf,n);
	return *this;
}
*/


stream &stream::operator << (double d)
{
	char buf[64];
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_FLOAT_NANO
	int n = snprintf(buf,sizeof(buf),"%4.1f",d);
	write(buf,n);
#else
	char *e = float_to_str(buf,d);
	write(buf,e-buf);
#endif
	return *this;
}


/*
nullstream::~nullstream()
{

}
*/


