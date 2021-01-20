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

#ifndef BUFFERED_TERMINAL_H
#define BUFFERED_TERMINAL_H

#include "terminal.h"
#include <stdint.h>


class MemTerminal : public Terminal
{
	public:
	explicit MemTerminal(const char *inp, size_t ilen, bool = false);
	explicit MemTerminal(bool = false);
	~MemTerminal();

	int read(char *, size_t, bool = true);
	int write(const char *b, size_t);

	const char *getBuffer() const
	{
		if (m_len <= m_asize)
			m_buf[m_len] = 0;
		return m_buf;
	}

	size_t getSize() const
	{ return m_len; }

	private:
	MemTerminal(const MemTerminal &);
	MemTerminal &operator = (const MemTerminal &);

	char *m_buf;
	const char *m_inp;
	size_t m_len, m_asize, m_ilen;
};


#endif
