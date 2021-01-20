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
, m_buf((char*)malloc(32))
, m_inp(0)
, m_len(0)
, m_asize(31)
, m_ilen(0)
{

}

MemTerminal::MemTerminal(const char *inp, size_t ilen, bool crnl)
: Terminal(crnl)
, m_buf((char*)malloc(64))
, m_inp(inp)
, m_len(0)
, m_asize(64)
, m_ilen(ilen)
{

}

MemTerminal::~MemTerminal()
{
	free(m_buf);
}


int MemTerminal::write(const char *buf, size_t s)
{
	if (m_buf == 0) {
		m_buf = (char*)malloc(s+1);
		m_asize = s;
	} else if (m_asize <= (m_len+s)) {
		m_asize = ((m_len+s+65)/64)*64;
		char *tmp = (char*)realloc(m_buf,m_asize);
		if (tmp == 0)
			free(m_buf);
		m_buf = tmp;
		m_asize = m_len+s;
	}
	if (m_buf == 0) {
		m_error = "out of memory";
		return -1;
	} else {
		memcpy(m_buf+m_len,buf,s);
		m_len += s;
	}
	return s;
}


int MemTerminal::read(char *buf, size_t s, bool b)
{
	if (m_inp == 0) {
		m_error = "nothing to read";
		errno = ENOTSUP;
		return -1;
	}
	size_t n = (s < m_ilen) ? s : m_ilen;
	memcpy(buf,m_inp,n);
	m_ilen -= n;
	if (n)
		m_inp += n;
	else
		m_inp = 0;
	return n;
}
