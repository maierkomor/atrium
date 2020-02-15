/*
 *  Copyright (C) 2018, Thomas Maier-Komor
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

#include "astream.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

using namespace std;


astream::astream(size_t s)
: stream()
, m_err(false)
{
	m_buf = (char *) malloc(s);
	m_end = m_buf + s;
	m_at = m_buf;
}

astream::~astream()
{
	if (m_buf)
		free(m_buf);
}


astream &astream::operator << (char c)
{
	if (c == '\n') {
		if (m_at < m_end) {
			*m_at++ = '\r';
		} else {
			m_err = true;
			return *this;
		}
	}
	if (m_at < m_end) {
		*m_at++ = c;
	} else {
		m_err = true;
	}
	return *this;
}


astream &astream::operator << (unsigned u)
{
	int n = snprintf(m_at,m_end-m_at,"%u",u);
	if (n < (m_end-m_at)) {
		m_at += n;
	} else {
		m_err = true;
		m_at = m_end;
	}
	return *this;
}


astream &astream::operator << (signed u)
{
	int n = snprintf(m_at,m_end-m_at,"%d",u);
	if (n < (m_end-m_at)) {
		m_at += n;
	} else {
		m_err = true;
		m_at = m_end;
	}
	return *this;
}


astream &astream::operator << (double d)
{
	int n = snprintf(m_at,m_end-m_at,"%G",d);
	if (n < (m_end-m_at)) {
		m_at += n;
	} else {
		m_err = true;
		m_at = m_end;
	}
	return *this;
}


astream &astream::operator << (const char *s)
{
	char c = *s;
	while (c) {
		if (m_at == m_end) {
			m_err = true;
			return *this;
		}
		if (c == '\n') {
			*m_at++ = '\r';
			if (m_at == m_end) {
				m_err = true;
				return *this;
			}
		}
		*m_at++ = c;
		++s;
		c = *s;
	}
	return *this;
	/*
	while (char *nl = strchr(nl)) {
		if ((nl - s) > (m_end - m_at))

	}
	size_t l = strlen(s);
	if (m_at + l < m_end) {
		memcpy(m_at,s,l);
		m_at += l;
	} else {
		m_err = true;
		memcpy(m_at,s,m_end-m_at);
		m_at = m_end;
	}
	return *this;
	*/
}


void astream::put(char c)
{
	if (c == '\n') {
		if (m_at < m_end) {
			*m_at++ = '\r';
		} else {
			m_err = true;
			return;
		}
	}
	if (m_at < m_end) {
		*m_at++ = c;
	} else {
		m_err = true;
	}
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


int astream::printf(const char *f, ...)
{
	va_list val;
	size_t nn = chrcnt(f,'\n');
	if (nn == 0) {
		va_start(val,f);
		int n = vsnprintf(m_at,m_end-m_at,f,val);
		va_end(val);
		m_at += n;
		if (m_at > m_end)
			m_at = m_end;
		return n;
	}
	char tmp[strlen(f)+nn+1], *t = tmp;
	while (const char *nl = strchr(f,'\n')) {
		memcpy(t,f,nl-f);
		t += nl-f;
		*t++ = '\r';
		*t++ = '\n';
		f = nl + 1;
	}
	*t = 0;
	va_start(val,f);
	int n = vsnprintf(m_at,m_end-m_at,tmp,val);
	va_end(val);
	m_at += n;
	if (m_at > m_end)
		m_at = m_end;
	return n;
}


size_t chrcntn(const char *s, char c, size_t n)
{
	size_t r = 0;
	while (const char *f = (const char *)memchr(s,c,n)) {
		++r;
		s = f+1;
	}
	return r;
}


void astream::write(const char *s, size_t l)
{
	size_t nn = chrcntn(s,'\n',l);
	if ((l+nn) >= (m_end - m_at)) {
		size_t nl = l + nn + (m_end-m_buf);
		char *nb = (char *)realloc(m_buf,nl);
		if (nb == 0) {
			m_err = true;
			return;
		}
		m_at = nb + (m_at-m_buf);
		m_end = nb + nl;
		m_buf = nb;
	}
	while (l) {
		const char *nl = (const char *)memchr(s,'\n',l);
		if (nl) {
			memcpy(m_at,s,nl-s);
			*m_at++ = '\r';
			*m_at++ = '\n';
			l -= (nl-s)+1;
			s = nl + 1;
			continue;
		} else {
			memcpy(m_at,s,l);
			m_at += l;
			l = 0;
		}
	}
}

