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

#include "estring.h"

#include <cassert>

#if 1
#define con_print(...)
#define con_printf(...)
#else
int con_print(const char *,...);
#endif

estring::estring()
: str((char*)malloc(1))
, len(0)
, alloc(1)
{
	con_print("estring()");
	str[0] = 0;
}


estring::estring(const char *s)
: str(0)
, alloc(0)
{
	con_printf("estring(s) %s\n",s);
	assign(s,strlen(s));
}


estring::estring(const char *s, size_t l)
: str(0)
, alloc(0)
{
	con_printf("estring(s,l) %s\n",s);
	assign(s,l);
}


estring::estring(const char *s, const char *e)
: str(0)
, alloc(0)
{
	con_printf("estring(s,e) %s\n",s);
	assign(s,e-s);
}


estring::estring(const estring &a)
: str((char*)malloc(a.alloc))
, len(a.len)
, alloc(a.alloc)
{
	con_printf("estring(const estring &) %s\n",a.str?a.str:"<null>");
	memcpy(str,a.str,len+1);
}


/*
estring::estring(const estring &&a)
: str((char*)malloc(a.alloc))
, len(a.len)
, alloc(a.alloc)
{
	con_printf("estring(const estring &&) %s\n",a.str);
	memcpy(str,a.str,len+1);
}


estring::estring(estring &&a)
: str(a.str)
, len(a.len)
, alloc(a.alloc)
{
	con_printf("estring(estring &&) %s\n",str);
	a.len = 0;
	a.alloc = 0;
}
*/


estring::~estring()
{
	con_printf("~estring() %s\n",str);
	free(str);
}


void estring::reserve(size_t ns)
{
	if (ns < alloc)
		return;
	// must always allocate ns+1 byte at minimum for trailing \0
	alloc = (ns+16) & ~15;	// reserve ns + [1..16]
	con_printf("reserve(%d) %d",ns,alloc);
	char *n = (char *)realloc(str,alloc);
	assert(n);
	str = n;
}


estring &estring::operator = (const estring &a)
{
	con_printf("operator =(const estring &) %s\n",a.str);
	reserve(a.len);
	memcpy(str,a.str,a.len+1);
	len = a.len;
	return *this;
}


estring &estring::operator += (const estring &a)
{
	size_t nl = len + a.len;
	reserve(nl);
	memcpy(str+len,a.str,a.len+1);
	len = nl;
	return *this;
}

bool estring::operator == (const estring &a) const
{
	return 0 == strcmp(str,a.str);
}


bool estring::operator != (const estring &a) const
{
	return 0 != strcmp(str,a.str);
}


estring &estring::operator = (const char *s)
{
	len = 0;
	return operator += (s);
}


estring &estring::operator += (const char *s)
{
	if (s) {
		size_t l = strlen(s);
		reserve(l+len);
		memcpy(str+len,s,l+1);
		len += l;
	}
	return *this;
}


estring &estring::operator += (char c)
{
	push_back(c);
	return *this;
}


void estring::push_back(char c)
{
	str[len] = c;
	++len;
	reserve(len);
	str[len] = 0;
}


void estring::assign(const char *m, size_t s)
{
	len = 0;
	append(m,s);
}


void estring::append(const char *m, size_t s)
{
	reserve(len+s);
	memcpy(str+len,m,s);
	len += s;
	str[len] = 0;
}


void estring::resize(size_t s, char c)
{
	reserve(s);
	if (s > len)
		memset(str+len,c,s-len);
	len = s;
}
