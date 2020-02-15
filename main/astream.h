/*
 *  Copyright (C) 2018-2019, Thomas Maier-Komor
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

#ifndef ASTREAM_H
#define ASTREAM_H

#include "stream.h"

#include <sys/types.h>

class astream : public stream
{
	public:
	astream(size_t s = 512);
	~astream();

	astream &operator << (unsigned);
	astream &operator << (signed);
	/*
	astream &operator << (unsigned long);
	astream &operator << (signed long);
	astream &operator << (unsigned long long);
	astream &operator << (signed long long);
	astream &operator << (float);
	*/
	astream &operator << (double);
	astream &operator << (const char *);
	astream &operator << (char);

	void put(char c);
	int printf(const char *, ...);
	void write(const char *s, size_t n);

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

	private:
	astream(const astream &);
	astream &operator = (const astream &);
};


#endif
