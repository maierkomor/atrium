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

#ifndef MSTREAM_H
#define MSTREAM_H

#include "stream.h"

#include <sys/types.h>

class mstream : public stream
{
	public:
	mstream(char *buf, size_t l)
	: stream()
	, m_buf(buf)
	, m_at(buf)
	, m_end(buf+l)
	, m_err(false)
	{ }

	~mstream();

	int put(char c);
	int printf(const char *, ...);
	int write(const char *s, size_t n);

	// !!! writing to mstream does not create terminating \0.
	// Use c_str() for printing the buffer. It will create a \0.
	const char *c_str()
	{
		if (m_at < m_end) {
			*m_at = 0;
		} else {
			m_end[-1] = 0;
			m_err = true;
		}
		return m_buf;
	}

	bool had_error() const
	{ return m_err; }

	char *at() const
	{ return m_at; }

	size_t size() const
	{ return m_at - m_buf; }

	void reset()
	{
		m_at = m_buf;
		m_err = false;
	}

	private:
	char *m_buf, *m_at, *m_end;
	bool m_err;
};


#endif
