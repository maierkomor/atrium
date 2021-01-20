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

#ifdef CONFIG_DHT

#include "actions.h"
#include "binformats.h"
#include "dht.h"
#include "dhtdrv.h"
#include "globals.h"
#include "influx.h"
#include "ujson.h"
#include "log.h"
#include "mqtt.h"
#include "shell.h"
#include "support.h"
#include "terminal.h"

#include <driver/gpio.h>

static DHT Dht;
static char TAG[] = "DHT";
static bool Enabled = false;


int dht_init()
{
	if (!HWConf.has_dht())
		return 1;
	if (Enabled)
		return 1;
	const DhtConfig &c = HWConf.dht();
	if (Dht.init(c.gpio(), c.model()))
		return 1;
	Enabled = true;
	return 0;
}


static void gatherData(void *)
{
	if (!Enabled)
		return;
	Dht.read();
	if (const char *err = Dht.getError()) {
		log_info(TAG,"driver error: %s",err);
		Temperature->set(NAN);
		Humidity->set(NAN);
	} else {
		double t = Dht.getTemperature();
		double h = Dht.getHumidity();
		rtd_lock();
		Temperature->set(t);
		Humidity->set(h);
		rtd_unlock();
		char buf[8];
		float_to_str(buf,t);
		log_info(TAG,"temperature %s\u00b0C",buf);
		float_to_str(buf,h);
		log_info(TAG,"humidity %s%%",buf);
	}
}


int dht(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return arg_invnum(term);
	if (argc == 2) {
		if (!strcmp(args[1],"enable"))
			Enabled = true;
		else if (!strcmp(args[1],"disable"))
			Enabled = false;
		else if (!strcmp(args[1],"sample"))
			gatherData(0);
		else
			return arg_invalid(term,args[1]);
	} else if (!Enabled) {
		term.printf("not intialized\n");
	} else if (const char *e = Dht.getError()) {
		term.printf("dht error: %s\n",e);
	} else {
		char buf[12];
		float_to_str(buf,Dht.getTemperature());
		term.printf("temperature: %s \u00bC\n",buf);
		float_to_str(buf,Dht.getHumidity());
		term.printf("humidity  : %s %%\n",buf);
	}
	return 0;
}


int dht_setup(void)
{
	if (!HWConf.has_dht())
		return 0;
	if (Temperature == 0) {
		Temperature = RTData->add("temperature",NAN);
		Temperature->setDimension("\u00b0C");
	}
	if (Humidity == 0) {
		Humidity = RTData->add("humidity",NAN);
		Humidity->setDimension("%");
	}
	if (dht_init())
		return 1;
	action_add("dht!sample",gatherData,0,"poll DHT data");
	return 0;
}


#endif
