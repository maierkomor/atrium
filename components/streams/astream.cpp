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


int astream::put(char c)
{
	if (m_at+3 >= m_end)
		resize(16);
	if (m_err)
		return -1;
	int r = 1;
	if (c == '\n') {
		*m_at++ = '\r';
		++r;
	}
	*m_at++ = c;
	*m_at = 0;
	return r;
}


int astream::resize(size_t ns)
{
	ns += m_at-m_buf;
	char *nb = (char *)realloc(m_buf,ns);
	if (nb == 0) {
		m_err = true;
		return 1;
	}
	m_at = nb + (m_at-m_buf);
	m_end = nb + ns;
	m_buf = nb;
	return 0;
}


int astream::write(const char *s, size_t l)
{
	int r = l;
	size_t nn = chrcntn(s,'\n',l);
	size_t rs = l + nn + 1;
	if (rs > (m_end - m_at)) {
		if (resize(rs))
			return -1;
	}
	while (l) {
		if (const char *nl = (const char *)memchr(s,'\n',l)) {
			memcpy(m_at,s,nl-s);
			*m_at++ = '\r';
			*m_at++ = '\n';
			++r;
			l -= (nl-s)+1;
			s = nl + 1;
		} else {
			memcpy(m_at,s,l);
			m_at += l;
			l = 0;
		}
	}
	assert(m_at < m_end);
	*m_at = 0;
	return r;
}

