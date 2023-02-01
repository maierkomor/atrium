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

#include <sdkconfig.h>

#ifndef CONFIG_SOCKET_API

#include "inetd.h"
#include "log.h"
#include "lwtcp.h"
#include "netsvc.h"
#include "terminal.h"

#include <assert.h>
#include <string.h>

const char *inetadm(Terminal &term, int argc, const char *args[])
{
	LwTcpListener *l = LwTcpListener::getFirst();
	if (argc == 2) {
		if (!strcmp(args[1],"-l")) {
			term.printf("%5s status %-10s\n","port","service");
			while (l) {
				term.printf("%5u %4s   %-10s\n",l->getPort(),l->isEnabled() ? "on" : "off",l->getName());

				l = l->getNext();
			}
			return 0;
		}
		return "Invalid argument #1.";;
	}
	if (argc != 3)
		return "Invalid number of arguments.";
	long p = strtol(args[2],0,0);
	while (l) {
		if ((l->getPort() == p) || (!strcmp(l->getName(),args[2])))
			break;
		 l = l->getNext();
	}
	if (l == 0)
		return "Invalid argument #2.";;
	bool e = l->isEnabled();
	const char *r;
	if (!strcmp(args[1],"-e")) {
		if (e) {
			return "Already enabled.";
		}
		l->enable(true);
		r = "enabled service";
	} else if (!strcmp(args[1],"-d")) {
		if (!e) {
			return "Already disabled.";
		}
		l->enable(false);
		r = "disabled service";
	} else {
		return "Invalid argument #1.";;
	}
	term.println(r);
	return 0;
}

#endif
