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


int Terminal::get_ch(char *c)
{
	return read(c,1,true);
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


/*
int arg_invnum(Terminal &t)
{
	t.println("invalid number of arguments");
	return 1;
}


int arg_invalid(Terminal &t, const char *a)
{
	t.printf("invalid argument '%s'\n",a);
	return 1;
}


int arg_range(Terminal &t, const char *a)
{
	t.printf("value '%s' out of range\n",a);
	return 1;
}


int arg_missing(Terminal &t)
{
	t.println("missing argument");
	return 1;
}


int arg_priv(Terminal &t)
{
	t.println("Access denied. Use 'su' to get access.");
	return 1;
}


int err_oom(Terminal &t)
{
	t.println("Out of memory.");
	return -2;
}
*/
