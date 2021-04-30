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

#ifndef STREAM_H
#define STREAM_H

#include "estring.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>


class stream
{
	public:
	explicit stream(bool crnl = false)
	: m_crnl(crnl)
	{ }

	virtual ~stream() = 0;

	stream &operator << (char c)
	{ print(&c,1); return *this; }
	stream &operator << (bool);
	stream &operator << (uint64_t);
	stream &operator << (int64_t);

	stream &operator << (int8_t v)
	{ return operator << ((int64_t) v); }

	stream &operator << (uint8_t v)
	{ return operator << ((uint64_t) v); }

	stream &operator << (int16_t v)
	{ return operator << ((int64_t) v); }

	stream &operator << (uint16_t v)
	{ return operator << ((uint64_t) v); }

	stream &operator << (int32_t v)
	{ return operator << ((int64_t) v); }

	stream &operator << (uint32_t v)
	{ return operator << ((uint64_t) v); }

	stream &operator << (double);

	stream &operator << (const char *s)
	{ print(s); return *this; }

	stream &operator << (const estring &s)
	{ print(s.data(),s.size()); return *this; }

	void println();
	void println(const char *);
	int print(const char *, size_t = 0);
	virtual int printf(const char *, ...);
	int vprintf(const char *, va_list);

	int put(char c)
	{
		if (m_crnl && (c == '\n'))
			return write("\r\n",2);
		return write(&c,1);
	}

	virtual int write(const char *s, size_t n) = 0;

	bool get_crnl() const
	{ return m_crnl; }

	void set_crnl(bool crnl)
	{ m_crnl = crnl; }

	protected:
	bool m_crnl;
};


inline stream::~stream()
{

}


/*
class nullstream : public stream
{
	public:
	~nullstream();

	nullstream &operator << (unsigned short)
	{ return *this; }
	nullstream &operator << (signed short)
	{ return *this; }
	nullstream &operator << (unsigned)
	{ return *this; }
	nullstream &operator << (signed)
	{ return *this; }
	nullstream &operator << (unsigned long)
	{ return *this; }
	nullstream &operator << (signed long)
	{ return *this; }
	nullstream &operator << (float)
	{ return *this; }
	nullstream &operator << (double)
	{ return *this; }
	nullstream &operator << (const char *)
	{ return *this; }
	nullstream &operator << (char)
	{ return *this; }

	int put(char c)
	{ return 0; }

	int printf(const char *, ...)
	{ return 0; }
	int write(const char *s, size_t n)
	{ return 0; }
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
	countstream &operator << (unsigned short a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (signed short a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (float a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (double a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (const char *a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (signed char a)
	{ m_cnt += sizeof(a); return *this; }
	countstream &operator << (unsigned char a)
	{ m_cnt += sizeof(a); return *this; }

	int printf(const char *, ...);

	int put(char c)
	{ ++m_cnt; return 1; }

	int write(const char *s, size_t n)
	{ m_cnt += n; return n; }

	size_t count() const
	{ return m_cnt; }

	private:
	size_t m_cnt;
};
*/

size_t chrcnt(const char *s, char c);
size_t chrcntn(const char *s, char c, size_t n);
char *float_to_str(char *buf, float f);

#endif
