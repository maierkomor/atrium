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

#include <set>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace std;

struct cstrcmp
{
	bool operator () (const char *l, const char *r) const
	{ return strcmp(l,r) < 0; }
};

static set<const char *,cstrcmp> Modules;
static bool EnableAll = false;


void log_module_print(Terminal &t)
{
	if (EnableAll)
		t.println("*");
	for (auto &m : Modules)
		t.println(m);
}


int log_module_enable(const char *m)
{
	log_info("log","debug %s",m);
	if (strcmp(m,"*"))
		return Modules.emplace(strdup(m)).second ? 0 : 1;
	EnableAll = true;
	return 0;
}


int log_module_enabled(const char *m)
{
	return Modules.find(m) != Modules.end();
}


int log_module_disable(const char *m)
{
	if (strcmp(m,"*")) {
		set<const char *,cstrcmp>::iterator i = Modules.find(m);
		if (i == Modules.end())
			return 1;
		free((void*)*i);
		Modules.erase(i);
		return 0;
	}
	if (!EnableAll)
		return 1;
	EnableAll = false;
	return 0;
}


void log_hex(const char *m, const uint8_t *d, unsigned n)
{
	char line[32], *at = line;
	unsigned x = 0;
	va_list val;
	while (x != n) {
		sprintf(at,"%02x",d[x]);
		at += 2;
		++x;
		if ((x & 7) == 0) {
			*at = 0;
			log_common(ll_debug,m,line,val);
			at = line;
			continue;
		} else if ((x & 3) == 0)
			*at++ = ' ';
		*at++ = ' ';
	}
	*at = 0;
	log_common(ll_debug,m,line,val);
}


void log_dbug(const char *m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	if (EnableAll || (Modules.find(m) != Modules.end())) {
		log_common(ll_debug,m,f,val);
//		vTaskDelay(20);
	}
	va_end(val);
}


