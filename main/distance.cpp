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

#ifdef CONFIG_DIST

#include "actions.h"
#include "globals.h"
#include "hc_sr04.h"
#include "hwcfg.h"
#include "log.h"
#include "ujson.h"

#include <stdlib.h>

static const char TAG[] = "hcsr04";
static HC_SR04 *Driver = 0;


int measure(unsigned *v)
{
	if (Driver == 0)
		return -1;
	return Driver->measure(v);
}


static void hcsr04_sample(void *arg)
{
	HC_SR04 *drv = (HC_SR04 *) arg;
	drv->trigger();
}


int distance_setup()
{
	for (const auto &c : HWConf.hcsr04()) {
		if (!c.has_trigger() || !c.has_echo()) {
			log_dbug(TAG,"incomplete config");
			continue;
		}
		if (HC_SR04 *drv = HC_SR04::create(c.trigger(),c.echo())) {
			if (c.has_name())
				drv->setName(c.name().c_str());
			const char *name = drv->getName();
			action_add(concat(name,"!sample"),hcsr04_sample,drv,"trigger a distance measurement");
			drv->attach(RTData);
		}
	}
	return 0;
}

#endif
