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

#include <sdkconfig.h>

#ifdef CONFIG_DIST
#include "distance.h"

#include "hc_sr04.h"
#include "log.h"
#include "terminal.h"

#include <stdlib.h>

static HC_SR04 *Driver = 0;


int measure(unsigned *v)
{
	if (Driver == 0)
		return -1;
	return Driver->measure(v);
}


int distance(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("%sinitialized\n",Driver ? "" : "not ");
	} else if (argc == 2) {
		if (0 == strcmp(args[1],"-h")) {
			term.printf("synopsis:\n%s init <trigger> <echo>\n%s poll\n",args[0],args[0]);
		} else if (0 == strcmp(args[1],"poll")) {
			unsigned v;
			if (0 != Driver->measure(&v)) {
				term.printf("\nmeasurement error\n");
				return 1;
			}
			term.printf("result: %u\n",v);
		} else if (0 == strcmp(args[1],"detach")) {
			delete Driver;
			Driver = 0;
		} else 
			return 1;
	} else if ((argc == 4) && (0 == strcmp(args[1],"init"))) {
		if (Driver != 0) {
			term.printf("already configured");
			return 1;
		}
		long t = strtol(args[2],0,0);
		long e = strtol(args[3],0,0);
		if ((t < 0) || (e < 0) || (t >= GPIO_NUM_MAX) || (e >= GPIO_NUM_MAX)) {
			term.printf("invalid argument");
			return 1;
		}
		Driver = new HC_SR04;
		if (int r = Driver->init(t,e)) {
			term.printf("result: %d\n",r);
			return 1;
		}
	} else
		return 1;
	return 0;
}


int distance_setup()
{
	Driver = new HC_SR04;
	int r = Driver->init(CONFIG_DIST_TRIGGER,CONFIG_DIST_ECHO);
	if (r) {
		delete Driver;
		Driver = 0;
		log_error("distance","driver setup failed with error %d",r);
	} else {
		log_info("distance","setup ok");
	}
	return 0;
}

#endif
