/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#ifdef CONFIG_DHT

#include "actions.h"
#include "dht.h"
#include "dhtdrv.h"
#include "globals.h"
#include "hwcfg.h"
#include "influx.h"
#include "env.h"
#include "log.h"
#include "mqtt.h"
#include "support.h"
#include "terminal.h"

#include <driver/gpio.h>

#define TAG MODULE_DHT
static DHT *Dht = 0;


int dht_init(EnvObject *root)
{
	if (!HWConf.has_dht())
		return 1;
	if (Dht)
		return 1;
	const DhtConfig &c = HWConf.dht();
	Dht = new DHT;
	if (Dht->init(c.gpio(), c.model()))
		return 1;
	Dht->attach(root);
	return 0;
}


static void gatherData(void *)
{
	if (Dht != 0)
		Dht->read();
}


const char *dht(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return "Invalid number of arguments.";
	if (argc == 2) {
		if (!strcmp(args[1],"sample"))
			gatherData(0);
		else
			return "Invalid argument #1.";
	} else if (Dht == 0) {
		return "No DHT found.";
	} else {
		char buf[12];
		float_to_str(buf,Dht->getTemperature());
		term.printf("temperature: %s \u00bC\n",buf);
		float_to_str(buf,Dht->getHumidity());
		term.printf("humidity   : %s %%\n",buf);
	}
	return 0;
}


int dht_setup(void)
{
	if (!HWConf.has_dht())
		return 0;
	if (dht_init(RTData))
		return 1;
	action_add("dht!sample",gatherData,0,"poll DHT data");
	return 0;
}


#endif
