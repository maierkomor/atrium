/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#define INI_BUF_SIZE 256


MemTerminal::MemTerminal(bool crnl)
: Terminal(crnl)
, m_buf(0)
, m_inp(0)
, m_len(0)
, m_asize(0)
, m_ilen(0)
{
}


MemTerminal::MemTerminal(const char *inp, size_t ilen, bool crnl)
: Terminal(crnl)
, m_buf(0)
, m_inp(inp)
, m_len(0)
, m_asize(0)
, m_ilen(ilen)
{
}


MemTerminal::~MemTerminal()
{
	if (m_buf)
		free(m_buf);
}


int MemTerminal::write(const char *buf, size_t s)
{
	if (m_error)
		return -1;
	if (m_buf == 0) {
		m_buf = (char*)malloc(INI_BUF_SIZE);
		if (m_buf) {
			m_asize = INI_BUF_SIZE;
		} else {
			m_error = "Out of memory.";
			return -1;
		}
	} else if (m_asize <= (m_len+s)) {
		m_asize = ((m_len+s+65)/64)*64;
		char *tmp = (char*)realloc(m_buf,m_asize);
		if (tmp == 0) {
			m_error = "Out of memory.";
			return -1;
		}
		m_buf = tmp;
		m_asize = m_len+s;
	}
	memcpy(m_buf+m_len,buf,s);
	m_len += s;
	return s;
}


int MemTerminal::read(char *buf, size_t s, bool b)
{
	if (m_inp == 0)
		return -1;	// end-of-file
	size_t n = (s < m_ilen) ? s : m_ilen;
	if (n) {
                if (n > 1)
                        memcpy(buf,m_inp,n);
                else
                        *buf = *m_inp;
                m_ilen -= n;
		m_inp += n;
        } else {
		m_inp = 0;
        }
	return n;
}
