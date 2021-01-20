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

#include "mstream.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <assert.h>

using namespace std;


mstream::~mstream()
{

}


int mstream::put(char c)
{
	int r = 1;
	if (m_crnl && (c == '\n') && (m_at < m_end)) {
		*m_at++ = '\r';
		++r;
	}
	if (m_at < m_end) {
		*m_at++ = c;
		return r;
	} else {
		m_err = true;
		return -1;
	}
}


int mstream::printf(const char *f, ...)
{
	// replaces stream::printf to write directly to the correct buffer
	va_list val;
	va_start(val,f);
	int n = vsnprintf(m_at,m_end-m_at,f,val);
	va_end(val);
	int r = n;
	if (m_crnl) {
		size_t nn = m_crnl ? chrcnt(f,'\n') : 0;
		if (m_at+n+nn > m_end) {
			m_err = true;
			return -1;
		}
		while (char *nl = (char*)memchr(m_at,'\n',n)) {
			*nl = '\r';
			++nl;
			n -= nl-m_at;
			memmove(nl+1,nl,n+1);
			*nl = '\n';
			m_at = nl+1;
		}
	}
	m_at += n;
	return r;
}


int mstream::write(const char *s, size_t l)
{
	// write ignores new-line translation!
	if (l > (m_end-m_at)) {
		m_err = true;
		return -1;
	}
	memcpy(m_at,s,l);
	m_at += l;
	return l;
}


#ifdef TEST_MODULE
int main()
{
	const char *test1 = "a2\n\nbc1\ncde1\nf\n";
	const char *test2 = "a\n";
	const char *test3 = "\nabc";
	char buf[128];
	mstream str(buf,sizeof(buf));
	str.set_crnl(true);
	str << test1;
	printf("1:'%*s'\n",(int)str.size(),buf);
	str.reset();
	str << test2;
	printf("2:'%*s'\n",(int)str.size(),buf);
	str.reset();
	str << test3;
	printf("3:'%*s'\n",(int)str.size(),buf);
	str.reset();
	str.printf("%s\n%s\n%s\n\n",test1,test2,test3);
	printf("4:'%*s'\n",(int)str.size(),buf);
}
#endif
