/*
 *  Copyright (C) 2018, Thomas Maier-Komor
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

#ifndef STREAM_H
#define STREAM_H

#include <sys/types.h>

class stream
{
	public:
	virtual ~stream() = 0;

	virtual stream &operator << (unsigned) = 0;
	virtual stream &operator << (signed) = 0;
	virtual stream &operator << (double) = 0;
	virtual stream &operator << (const char *) = 0;
	virtual stream &operator << (char) = 0;

	virtual void put(char c) = 0;

	virtual int printf(const char *, ...) = 0;
	virtual void write(const char *s, size_t n) = 0;
};


inline stream::~stream()
{

}


class nullstream : public stream
{
	public:
	~nullstream();

	nullstream &operator << (unsigned)
	{ return *this; }
	nullstream &operator << (signed)
	{ return *this; }
	nullstream &operator << (unsigned long)
	{ return *this; }
	nullstream &operator << (signed long)
	{ return *this; }
	nullstream &operator << (unsigned long long)
	{ return *this; }
	nullstream &operator << (signed long long)
	{ return *this; }
	nullstream &operator << (float)
	{ return *this; }
	nullstream &operator << (double)
	{ return *this; }
	nullstream &operator << (const char *)
	{ return *this; }
	nullstream &operator << (char)
	{ return *this; }

	void put(char c)
	{ }

	int printf(const char *, ...)
	{ return 0; }
	void write(const char *s, size_t n)
	{ }
};


class countstream : public stream
{
	public:
	countstream()
	: m_cnt(0)
	{ }

	~countstream();

	countstream &operator << (unsigned a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (signed a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (unsigned long a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (signed long a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (unsigned long long a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (signed long long a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (float a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (double a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (const char *a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (char a)
	{ m_cnt += sizeof(a); return *this; }

	void put(char c)
	{ ++m_cnt; }

	int printf(const char *, ...);
	void write(const char *s, size_t n)
	{ m_cnt += n; }

	size_t count() const
	{ return m_cnt; }

	private:
	size_t m_cnt;
};

size_t chrcnt(const char *s, char c);
size_t chrcntn(const char *s, char c, size_t n);

#endif
