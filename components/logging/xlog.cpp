/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

static set<estring,estring_cmp> Modules;
static bool EnableAll = false;


void log_module_print(Terminal &t)
{
	if (EnableAll)
		t.println("*");
	for (auto &m : Modules)
		t.println(m.c_str());
}


int log_module_enable(const char *m)
{
	log_info("log","debug %s",m);
	if (strcmp(m,"*"))
		return Modules.emplace(m).second ? 0 : 1;
	EnableAll = true;
	return 0;
}


int log_module_disable(const char *m)
{
	if (strcmp(m,"*"))
		return (Modules.erase(m) == 0);
	if (!EnableAll)
		return 1;
	EnableAll = false;
	return 0;
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


