/*
 *  Copyright (C) 2018-2025, Thomas Maier-Komor
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

#include "terminal.h"
#include <cassert>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>


HistBuf::HistBuf(size_t s)
{
	if (0 != s) {
		m_buf = (char *) malloc(s);
		m_in = m_buf;
		m_end = m_buf + s;
	}
}

int HistBuf::resize(size_t s)
{
	if (0 != s) {
		size_t in = m_in - m_buf;
		char *buf = (char *) realloc(m_buf,s);
		if (0 == buf)
			return 1;
		m_buf = buf;
		m_in = buf + in;
		m_end = buf + s;
	} else if (m_buf) {
		free(m_buf);
		m_buf = 0;
		m_end = 0;
		m_in = 0;
	}
	return 0;
}


int HistBuf::reserve(size_t r)
{
	if (r > (m_end-m_buf))
		return 1;
	if ((m_end - m_in) >= r)
		return 0;
	size_t av = m_end - m_in;
	char *at = m_buf;
	while (av < r) {
		size_t l = strlen(at);
		++l;
		at += l;
		av += l;
	}
	memmove(m_buf,at,m_in-at);
	m_in -= at-m_buf;
	return 0;
}


void HistBuf::add(const estring &str)
{
	size_t l = str.size();
	if (0 == l)
		return;
	++l;
	if (reserve(l))
		return;
	memcpy(m_in,str.c_str(),l);
	m_in += l;
}


void HistBuf::add(const char *str, size_t l)
{
	if ((0 == str) || (0 == *str))
		return;
	if (0 == l)
		l = strlen(str);
	++l;
	if (reserve(l))
		return;
	memcpy(m_in,str,l);
	m_in += l;
}


const char *HistBuf::get(int x) const
{
	if (m_in == m_buf)
		return 0;
	assert(m_in[-1] == 0);
	assert(m_in - m_buf > 1);
	if (x < 0) {
		x = -x;
		const char *at = m_in-1;
		do {
			--at;
			at = (const char*) memrchr(m_buf,0,at-m_buf);
			if (0 == at) {
				if (1 == x)
					return m_buf;
				return 0;
			}
		} while (--x);
		return at+1;
	} else {
		const char *at = m_buf;
		while (--x) {
			at = (const char *) memchr(at,0,m_in-at);
			if (0 == at)
				return 0;
			++at;
		};
		if (at < m_in)
			return at;
		return 0;
	}
}


#ifdef TEST_HISTBUF
#include <stdio.h>
#include <stdlib.h>
void printbuf(HistBuf &h)
{
	int x = 1;
	printf("### start of history\n");
	while (const char *str = h.get(x)) {
		printf("%d: %s\n",x,str);
		++x;
	}
	printf("### reverse history\n");
	x = -1;
	while (const char *str = h.get(x)) {
		printf("%d: %s\n",x,str);
		--x;
	}
	printf("### end of history\n");
}
int main()
{
	const char digits[] = "0123456789";
	const char alpha[] = "abcdefghijklmnopqrstuvwxyz";
	HistBuf h(128);
	for (;;) {
		long l = random();
		estring str(digits,l%sizeof(digits));
		printf("add '%s'\n",str.c_str());
		h.add(str);
		printbuf(h);
		estring str2(alpha,l%sizeof(alpha));
		printf("add '%s'\n",str2.c_str());
		h.add(str2);
		printbuf(h);
	}

}
#endif


Terminal::~Terminal()
{

}


/*
const char *Terminal::type() const
{
	return 0;
}


int Terminal::read(char *, size_t, bool block)
{
	return -1;
}
*/


int Terminal::get_ch(char *c)
{
	return read(c,1,true);
}


const estring &Terminal::getPwd()
{
	if (m_pwd.empty())
		m_pwd = "/flash/";
	return m_pwd;
}


int Terminal::readInput(char *buf, size_t l, bool echo)
{
	char *eob = buf + l - 2;	// save 2 bytes for \e8 transmission at end of loop
	char *at = buf, *eoi = buf;
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	int hidx=0;
#endif
	int n;
	char c;
	while (0 < (n = get_ch(&c))) {
		if ((c == '\n') || (c == '\r')) {
			if (echo) {
				write(&c,1);
				sync();
			}
			*eoi = 0;
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
			m_hist.add(buf,eoi-buf);
#endif
			return eoi-buf;
		}
		const char *estr = 0;
		bool rewrite = false;
		if ((c == 0x8) || (c == 0x7f)) {
			// backspace
			if (at != buf) {
				if (eoi != at) {
					rewrite = true;
					memmove(at-1,at,eoi-at);
					estr = "\e[D\e[K\e7";
				} else
					estr = "\e[D \e[D";
				--at;
				--eoi;
				if ((at > buf) && ((at[-1] & 0xf0) == 0xc0)) {
					if (eoi != at)
						memmove(at-1,at,eoi-at);
					--at;
					--eoi;
				}
			}
		} else if (c == 0x1b) {
			// escape sequence
			if (0 > get_ch(&c))
				return -1;
			if (c != '[')
				continue;
			if (0 > get_ch(&c))
				return -1;
			if ((c == 'C') && (at < eoi)) {
				// cursor right
				estr = "\e[C";
				++at;
				if ((at < eoi) && ((at[1] & 0xf0) == 0xc0))
					++at;
			} else if ((c == 'D') && (at > buf)) {
				// cursor left
				estr = "\e[D";
				--at;
				if ((at > buf) && ((at[-1] & 0xf0) == 0xc0))
					--at;
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
			} else if (c == 'A') {
				// cursor up
				--hidx;
				const char *he = m_hist.get(hidx);
				if (0 == he) {
					he = "";
					hidx = 0;
				}
				strncpy(buf,he,l);
				estr = "\e[3G\e[K";	// move to 3rd culomn of line, clear till end of line
				write(estr,strlen(estr));
				size_t hl = strlen(he);
				write(he,hl);
				eoi = buf + hl;
				at = eoi;
				estr = 0;
			} else if (c == 'B') {
				// cursor down
				++hidx;
				const char *he = m_hist.get(hidx);
				if (0 == he) {
					he = "";
					hidx = 0;
				}
				strncpy(buf,he,l);
				estr = "\e[3G\e[K";	// move to 3rd culomn of line, clear till end of line
				write(estr,strlen(estr));
				size_t hl = strlen(he);
				write(he,hl);
				eoi = buf + hl;
				at = eoi;
				estr = 0;
#endif
			} else if (c == '3') {
				if (0 > get_ch(&c))
					return -1;
				if (c != '~')
					continue;
				// \e[3~ is the delete key
				if (at < eoi) {
					memmove(at,at+1,eoi-at-1);
					--eoi;
					rewrite = true;
					estr = "\e7\e[K";
				}
			}
		} else  {
			// regular character
			if (at != eoi) {
				memmove(at+1,at,eob-at-1);
				rewrite = true;
				estr = "\e7";
			}
			*at = c;
			++at;
			++eoi;
			if (eoi == eob)
				eoi = eob-1;
			if (echo)
				write(&c,1);;
			if (at == eob) {
				at = eob-1;
				estr = "\e[D";
			}
		}
		if (echo) {
			if (estr && (0 > write(estr,strlen(estr))))
				return -1;
			if (rewrite) {
				// 2 bytes reserved above
				eoi[0] = '\e';
				eoi[1] = '8';
				if (0 > write(at,eoi-at+2))
					return -1;
			}
		}
	}
	if (n < 0)
		return -1;
        int x = eoi-buf;
        if ((n == 0) && (x == 0))
                return -1;
	return x;
}


void Terminal::print_hex(const uint8_t *b, size_t s, size_t off)
{
	const uint8_t *a = b, *e = b + s;
	while (a != e) {
		char tmp[64], *t = tmp;
		t += sprintf(t,"%04x: ",a-b+off);
		int i = 0;
		while ((a < e) && (i < 16)) {
			*t++ = ' ';
			if (i == 8)
				*t++ = ' ';
#if 0
			uint8_t d = *a;
			uint8_t h = d >> 4;
			if (h > 9)
				*t = 'a' - 10 + h;
			else
				*t = '0' + h;
			++t;
			uint8_t l = d & 0xf;
			if (l > 9)
				*t = 'a' - 10 + l;
			else
				*t = '0' + l;
			++t;
#else
			t += sprintf(t,"%02x",*a);
#endif
			++i;
			++a;
		}
		*t = 0;
		println(tmp);
	}
}


int Terminal::setPwd(const char *cd)
{
	if (cd == 0)
		return -EINVAL;
	if (cd[0] == '/') {
		m_pwd = cd;
		if (m_pwd.back() != '/')
			m_pwd.push_back('/');
		return 0;
	}
#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	return -1;
#else
	if (m_pwd.empty())
		m_pwd = "/flash/";
	while (cd && cd[0]) {
		while ((cd[0] == '.') && ((cd[1] == '/') || cd[1] == 0)) {
			if (cd[1])
				cd += 2;
			else
				++cd;
		}
		while ((cd[0] == '.') && (cd[1] == '.') && ((cd[2] == '/') || cd[2] == 0)) {
			// strip trailing slash
			if (m_pwd.size() > 1)
				m_pwd.resize(m_pwd.size()-1);
			const char *p = m_pwd.c_str();
			// find last slash
			const char *ls = strrchr(p,'/');
			if (p != ls)
				m_pwd.resize(ls-p + 1);
			if (cd[2])
				cd += 3;
			else
				cd += 2;
		}
		if (cd[0]) {
			const char *sl = strchr(cd,'/');
			if (sl) {
				m_pwd.append(cd,sl-cd+1);
				cd = sl + 1;
			} else {
				m_pwd += cd;
				m_pwd += '/';
				cd = 0;
			}
		}
	}
	if (m_pwd.back() != '/')
		m_pwd.push_back('/');
	return 0;
#endif
}
