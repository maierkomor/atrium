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

#ifdef CONFIG_BME280

#include "actions.h"
#include "binformats.h"
#include "bme.h"
#include "bme280.h"
#include "globals.h"
#include "influx.h"
#include "ujson.h"
#include "log.h"
#include "support.h"
#include "terminal.h"

#include <driver/gpio.h>

static const char TAG[] = "BME";
static bool Enabled = false;


static void gatherData(void *ignored)
{
	if (!Enabled)
		return;
	double t,h,p;
	if (int e = bme280_read(&t,&h,&p)) {
		log_warn(TAG,"driver error: %d",e);
		t = NAN;
		h = NAN;
		p = NAN;
	} else {
		p /= 100;	// convert from Pa to hPa
	}
	rtd_lock();
	Temperature->set(t);
	Humidity->set(h);
	Pressure->set(p);
	rtd_unlock();
	/*
	char temp[12],humid[12],press[12];
	strcpy(float_to_str(temp,t)," \u00b0C");
	strcpy(float_to_str(humid,h)," %");
	strcpy(float_to_str(press,p)," hPa");
	log_dbug(TAG,"%s, %s, %s",temp,humid,press);
	*/
}


int bme_setup(void)
{
	if (!HWConf.has_bme280()) {
		log_info(TAG,"no config");
		return 0;
	}
	const Bme280Config &c = HWConf.bme280();
	if (!c.has_sda() || !c.has_scl()) {
		log_info(TAG,"incomplete config");
		return 1;
	}
#ifdef CONFIG_IDF_TARGET_ESP8266
	unsigned f = 0;
#else
	unsigned f = c.freq();
#endif
	if (int i = bme280_init(c.port(),c.sda(),c.scl(),f)) {
		log_error(TAG,"init failed: %d",i);
		return 1;
	}
	if (Temperature == 0)
		Temperature = RTData->add("temperature",NAN);
	else
		log_warn(TAG,"json/Temperature already defined");
	if (Humidity == 0)
		Humidity = RTData->add("humidity",NAN);
	else
		log_warn(TAG,"json/Humidity already defined");
	if (Pressure == 0)
		Pressure = RTData->add("pressure",NAN);
	else
		log_warn(TAG,"json/Pressure already defined");
	action_add("bme280!sample",gatherData,0,"bme280: sample data");
	Enabled = true;
	log_info(TAG,"setup done");
	return 0;
}


int bme(Terminal &term, int argc, const char *args[])
{
	if (!HWConf.has_bme280()) {
		term.printf("not configured\n");
		return 1;
	}
	if (argc > 3) {
		term.printf("%s: 0-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 2) {
		if (!strcmp(args[1],"enable"))
			Enabled = true;
		else if (!strcmp(args[1],"setup"))
			return bme_setup();
		else if (!strcmp(args[1],"disable"))
			Enabled = false;
		else if (!strcmp(args[1],"sample"))
			gatherData(0);
		else {
			term.printf("invalid argument\n");
			return 1;
		}
		return 0;
	}
	if (Pressure == 0) {
		term.printf("not initialized\n");
		return 1;
	}
	float t = Temperature->get();
	float h = Humidity->get();
	float p = Pressure->get();
#ifdef CONFIG_IDF_TARGET_ESP8266
	char buf[12];
	if (!isnan(t)) {
		float_to_str(buf,t);
		term.printf("temperature: %s \u00b0C\n",buf);
	}
	if (!isnan(h)) {
		float_to_str(buf,h);
		term.printf("humidity   : %s %%\n",buf);
	}
	if (!isnan(p)) {
		float_to_str(buf,p);
		term.printf("pressure   : %s hPa\n",buf);
	}
#else
	if (!isnan(t))
		term.printf("temperature: %4.1f \u00b0C\n",(double) t);
	if (!isnan(h))
		term.printf("humidity   : %4.1f %%\n",(double) h);
	if (!isnan(p))
		term.printf("pressure   : %4.1f hPa\n",(double) p);
#endif
	return 0;
}

#endif
