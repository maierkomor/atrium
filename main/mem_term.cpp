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

#include "mem_term.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


MemTerminal::MemTerminal(bool crnl)
: Terminal(crnl)
, m_buf(0)
, m_len(0)
, m_asize(0)
{

}

MemTerminal::~MemTerminal()
{
	free(m_buf);
}


int MemTerminal::write(const char *buf, size_t s)
{
	if (m_buf == 0) {
		m_buf = (char*)malloc(s);
		m_asize = s;
	} else if (m_asize < (m_len+s)) {
		m_buf = (char*)realloc(m_buf,m_len+s);
		m_asize = m_len+s;
	}
	if (m_buf == 0) {
		m_error = "out of memory during write";
		return -1;
	} else {
		memcpy(m_buf+m_len,buf,s);
		m_len += s;
	}
	return s;
}


void MemTerminal::printf(const char *fmt, ...)
{
	int inc = 256, size = 80;
	do {
		if (m_asize - m_len < size) {
			m_asize += inc;
			char *b = (char*)realloc(m_buf,m_asize + inc);
			if (b == 0) {
				m_error = "out of memory";
				return;
			}
			m_asize += inc;
			m_buf = b;
		}
		va_list val;
		va_start(val,fmt);
		size = vsnprintf(m_buf+m_len,m_asize-m_len,fmt,val);
		inc = ((size>>8)+1)<<8;
		va_end(val);
	} while (size >= (m_asize-m_len));
	m_len += size;
}


void MemTerminal::put(const char *str, size_t n)
{
	if (m_len+n > m_asize) {
		m_asize = (((m_len+n)/128)+1)*128;
		m_buf = (char*)realloc(m_buf,m_asize);
		if (m_buf == 0) {
			m_error = "out of memory during write";
			return;
		}
	}
	memcpy(m_buf+m_len,str,n);
	m_len += n;
}


int MemTerminal::read(char *buf, size_t s, bool b)
{
	m_error = "nothing to read";
	errno = ENOTSUP;
	return -1;
}
