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

#include "astream.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

#if 1
#define con_print(...)
#define con_printf(...)
#else
extern "C" int con_printf(const char *,...);
#endif

using namespace std;


astream::astream(size_t s, bool crnl)
: stream(crnl)
{
	m_buf = (char *) malloc(s);
	if (m_buf)
		m_end = m_buf + s;
	else
		m_end = 0;
	m_at = m_buf;
}

astream::~astream()
{
	con_printf("~astream() %p",m_buf);
	free(m_buf);
}


char *astream::take()
{
	if (m_at == m_end)
		resize(1);
	assert(m_at < m_end);
	*m_at = 0;
	con_printf("astream::take() %s",m_buf);
	char *r = m_buf;
	m_buf = 0;
	m_end = 0;
	m_at = 0;
	return r;
}


const char *astream::c_str()
{
	if (m_at == m_end)
		resize(1);
	assert(m_at < m_end);
	*m_at = 0;
	con_printf("astream::c_str() %s",m_buf);
	return m_buf;
}


int astream::put(char c)
{
	if (m_at+3 >= m_end)
		resize(16);
	int r = 1;
	if (c == '\n') {
		*m_at++ = '\r';
		++r;
	}
	*m_at++ = c;
	return r;
}


int astream::resize(size_t ns)
{
	con_printf("astream %p::resize(%u)",this,ns);
	unsigned fill = m_at-m_buf;
	ns += fill;
	char *nb = (char *)realloc(m_buf,ns);
	if (nb == 0) {
		free(m_buf);
		m_buf = 0;
		m_at = 0;
		m_end = 0;
		con_printf("FAILED!");
		return 1;
	}
	m_at = nb + fill;
	m_end = nb + ns;
	m_buf = nb;
	con_printf("resize now %d",m_end-m_at);
	return 0;
}


int astream::write(const char *s, size_t l)
{
	if (m_end-m_at < l) {
		if (resize(l+1))
			return -1;
	}
	con_printf("astream::write('%.*s',%d):m_at=%p,m_end=%p,free=%d",l,s,l,m_at,m_end,m_end-m_at);
	memcpy(m_at,s,l);
	m_at += l;
	return l;
}

