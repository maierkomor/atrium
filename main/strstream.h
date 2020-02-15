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

#ifndef STRSTREAM_H
#define STRSTREAM_H

#include "stream.h"

#include <sys/types.h>
#include <string>

class strstream : public stream
{
	public:
	explicit strstream(std::string &str)
	: stream()
	, m_str(str)
	{ }

	~strstream();

	strstream &operator << (unsigned);
	strstream &operator << (signed);
	/*
	strstream &operator << (unsigned long);
	strstream &operator << (signed long);
	strstream &operator << (unsigned long long);
	strstream &operator << (signed long long);
	strstream &operator << (float);
	*/
	strstream &operator << (double);
	strstream &operator << (const char *);
	strstream &operator << (char);

	void put(char c);
	int printf(const char *, ...);
	void write(const char *s, size_t n);

	size_t size() const
	{ return m_str.size(); }

	void reset()
	{ m_str.clear(); }

	private:
	std::string &m_str;
};


#endif
