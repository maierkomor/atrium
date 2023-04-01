/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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

#include "estring.h"
#include "log.h"
#include "terminal.h"

#include <bitset>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG MODULE_LOG

using namespace std;

bitset<NUM_MODULES> Modules;


void log_module_print(Terminal &t)
{
	if (Modules[0])
		t.println("*");
	for (int i = 0; i < NUM_MODULES; ++i) {
		if (Modules[i])
			t.println(ModNames+ModNameOff[i]);
	}
}


int log_module_enable(const char *m)
{
	log_info(TAG,"debug %s",m);
	if (0 == strcmp(m,"*")) {
		Modules[0] = true;
		return 0;
	}
	for (int i = 1; i < NUM_MODULES; ++i) {
		if (0 == strcmp(m,ModNames+ModNameOff[i])) {
			Modules[i] = true;
			return 0;
		}
	}
	return 1;
}

void log_module_enable(const vector<estring> &mods)
{
#if 0
	estring mn;
	for (const auto &m : mods) {
		for (int i = 1; i < NUM_MODULES; ++i) {
			if (0 == strcmp(m.c_str(),ModNames+ModNameOff[i])) {
				Modules[i] = true;
				mn += m;
				mn += ',';
				break;
			}
		}
	}
	if (size_t s = mn.size())
		log_info(TAG,"debug %.*s",s-1,mn.data());
#else
	char mn[80], *at = mn;
	for (const auto &m : mods) {
		const char *c = m.c_str();
		size_t s = m.size();
		for (int i = 1; i < NUM_MODULES; ++i) {
			if (0 == memcmp(c,ModNames+ModNameOff[i],s+1)) {
				Modules[i] = true;
				if (at+s >= mn+sizeof(mn)) {
					at[-1] = 0;
					log_info(TAG,"debug %s",mn);
					at = mn;
				}
				memcpy(at,c,s);
				at += s;
				*at++ = ',';
			}
		}
	}
	if (at != mn) {
		at[-1] = 0;
		log_info(TAG,"debug %s",mn);
	}
#endif
}


int log_module_enabled(logmod_t m)
{
	return Modules[0] || Modules[m];
}


int log_module_disable(const char *m)
{
	if (strcmp(m,"*")) {
		for (int i = 1; i < NUM_MODULES; ++i) {
			if (0 == strcmp(m,ModNames+ModNameOff[i])) {
				Modules[i] = false;
				return 0;
			}
		}
	} else if (Modules[0]) {
		Modules[0] = false;
		return 0;
	}
	return 1;
}


void log_hex(logmod_t m, const void *data, unsigned n, const char *f, ...)
{
	const char *hex = "0123456789abcdef";
	if (!Modules[0]&& !Modules[m])
		return;
	const uint8_t *d = (const uint8_t *) data;
	va_list val;
	va_start(val,f);
	log_common(ll_debug,m,f,val);
	char line[80], *at = line;
	char ascii[20], *a = ascii;
	unsigned x = 0;
	*line = 0;
	while (x != n) {
		if (*line == 0) {
			at = line + sprintf(line,"%4d:",x);
			a = ascii;
		}
		*at++ = ' ';
		*at++ = hex[d[x]>>4];
		*at++ = hex[d[x]&0xf];
//		at += sprintf(at," %02x",d[x]);
		if ((d[x] >= 0x20) && (d[x] <= 0x7e))
			*a = d[x];
		else
			*a = ' ';
		++a;
		++x;
		if (((x & 15) == 0) || (x == n)) {
			*at = 0;
			*a = 0;
			log_direct(ll_debug,m,"%-56s '%s'",line,ascii);
			*line = 0;
		} else if ((x & 7) == 0)
			*at++ = ' ';
	}
	va_end(val);
}


void log_dbug(logmod_t m, const char *f, ...)
{
	if (Modules[0] || Modules[m]) {
		va_list val;
		va_start(val,f);
		log_common(ll_debug,m,f,val);
		va_end(val);
	}
}


void log_local(logmod_t m, const char *f, ...)
{
	if (Modules[0] || Modules[m]) {
		va_list val;
		va_start(val,f);
		log_common(ll_local,m,f,val);
		va_end(val);
	}
}


void log_gen(log_level_t ll, logmod_t m, const char *f, ...)
{
	if ((ll != ll_debug) || Modules[0] || Modules[m]) {
		va_list val;
		va_start(val,f);
		log_common(ll,m,f,val);
		va_end(val);
	}
}


