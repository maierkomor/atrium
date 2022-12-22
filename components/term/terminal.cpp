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

#include "terminal.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>


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
	int n;
	char c;
	while (0 < (n = get_ch(&c))) {
		if (c == '\n') {
			if (echo)
				write(&c,1);
			continue;
		}
		if (c == '\r') {
			if (echo) {
				write(&c,1);
				sync();
			}
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
			} else if ((c == 'D') && (at > buf)) {
				// cursor left
				estr = "\e[D";
				--at;
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
	return eoi-buf;
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
