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

#ifndef ASTREAM_H
#define ASTREAM_H

#include "stream.h"

#include <sys/types.h>

class astream : public stream
{
	public:
	explicit astream(size_t s = 512, bool crnl = false);
	~astream();

	int put(char c);
	int write(const char *s, size_t n);
	const char *c_str();

	char *at() const
	{ return m_at; }

	size_t size() const
	{ return m_at - m_buf; }

	const char *buffer() const
	{ return m_buf; }

	const char *data()
	{ return m_buf; }

	void reset()
	{ m_at = m_buf; }

	char *take();

	private:
	char *m_buf, *m_at, *m_end;

	astream(const astream &);
	astream &operator = (const astream &);
	int resize(size_t);
};


#endif
