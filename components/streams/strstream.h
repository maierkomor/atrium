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

#ifndef STRSTREAM_H
#define STRSTREAM_H

#include "stream.h"

#include <sys/types.h>
#include "estring.h"


class strstream : public stream
{
	public:
	explicit strstream(estring &str)
	: stream()
	, m_str(str)
	{ }

	~strstream();

	int put(char c);
	int printf(const char *, ...);
	int write(const char *s, size_t n);

	size_t size() const
	{ return m_str.size(); }

	void reset()
	{ m_str.clear(); }

	private:
	estring &m_str;
};


#endif
