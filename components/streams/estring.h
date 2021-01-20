/*
 *  Copyright (C) 2020, Thomas Maier-Komor
 *
 *  This source file belongs to Wire-Format-Compiler.
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

#ifndef _ESTRING_H
#define _ESTRING_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

class estring
{
	public:
	estring();
	estring(const char *s);
	estring(const char *s, size_t l);
	estring(const char *s, const char *e);
	estring(const estring &a);
//	estring(const estring &&a);
//	estring(estring &&a);
	~estring();
	estring &operator = (const estring &a);
	estring &operator += (const estring &a);
	bool operator == (const estring &a) const;
	bool operator != (const estring &a) const;
	estring &operator = (const char *s);
	estring &operator += (const char *s);
	estring &operator += (char c);
	void assign(const char *m, size_t s);
	void append(const char *m, size_t s);
	void resize(size_t,char = 0);

	bool operator == (const char *s) const
	{ return 0 == strcmp(str,s); }

	bool operator != (const char *s) const
	{ return 0 != strcmp(str,s); }

	const char *c_str() const
	{ return str; }

	void clear()
	{
		str[0] = 0;
		len = 0;
	}

	char back() const
	{ if (len == 0) return 0; else return str[len-1]; }

	void push_back(char c);

	const char *data() const
	{ return str; }

	bool empty() const
	{ return len == 0; }

	size_t size() const
	{ return len; }

	friend bool operator < (const estring &, const estring &);
	friend bool operator <= (const estring &, const estring &);
	friend bool operator > (const estring &, const estring &);
	friend bool operator >= (const estring &, const estring &);
	friend bool operator < (const estring &, const char *);
	friend bool operator <= (const estring &, const char *);
	friend bool operator > (const estring &, const char *);
	friend bool operator >= (const estring &, const char *);

	private:
	friend struct estring_cmp;
	void reserve(size_t);
	char *str;
	size_t len,alloc;
};


struct estring_cmp
{
	bool operator () (const estring &l, const estring &r) const
	{ return strcmp(l.str,r.str) < 0; }
};


inline bool operator < (const estring &l, const estring &r)
{
	return strcmp(l.str,r.str) < 0;
}


inline bool operator <= (const estring &l, const estring &r)
{
	return strcmp(l.str,r.str) <= 0;
}


inline bool operator < (const estring &l, const char *rs)
{
	return strcmp(l.str,rs) < 0;
}


inline bool operator <= (const estring &l, const char *rs)
{
	return strcmp(l.str,rs) <= 0;
}


inline bool operator > (const estring &l, const estring &r)
{
	return strcmp(l.str,r.str) > 0;
}


inline bool operator >= (const estring &l, const estring &r)
{
	return strcmp(l.str,r.str) >= 0;
}


inline bool operator > (const estring &l, const char *rs)
{
	return strcmp(l.str,rs) > 0;
}


inline bool operator >= (const estring &l, const char *rs)
{
	return strcmp(l.str,rs) >= 0;
}

#endif
