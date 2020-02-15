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

#include "mstream.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <assert.h>

using namespace std;


nullstream::~nullstream()
{

}


mstream::~mstream()
{

}


mstream &mstream::operator << (char c)
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


mstream &mstream::operator << (unsigned u)
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


mstream &mstream::operator << (signed u)
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


mstream &mstream::operator << (double d)
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


mstream &mstream::operator << (const char *s)
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


void mstream::put(char c)
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


int mstream::printf(const char *f, ...)
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


void mstream::write(const char *s, size_t l)
{
	while (l) {
		if (m_at == m_end) {
			m_err = true;
			return;
		}
		char c = *s++;
		if (c == '\n') {
			*m_at++ = '\r';
			if (m_at == m_end) {
				m_err = true;
				return;
			}
		}
		*m_at++ = c;
		--l;
	}
	/*
	if (m_at + l > m_end) {
		l = m_end - m_at;
		m_err = true;
	}
	memcpy(m_at,s,l);
	m_at += l;
	*/
}


