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

#ifdef CONFIG_BME280

#include "cyclic.h"
#include "bme.h"
#include "bme280.h"
#include "globals.h"
#include "influx.h"
#include "log.h"
#include "mqtt.h"
#include "support.h"
#include "terminal.h"

#include <driver/gpio.h>

static char TAG[] = "BME";
static unsigned Interval = 10000;
static bool Enabled = false, Initialized = false;


static unsigned gatherData()
{
	if (!Enabled)
		return Interval;
	double t,h,p;
	if (int e = bme280_read(&t,&h,&p)) {
		log_info(TAG,"driver error: %d",e);
		RTData.clear_temperature();
		RTData.clear_humidity();
		RTData.clear_pressure();
	} else {
		RTData.set_temperature(t);
		RTData.set_humidity(h);
		RTData.set_pressure(p);
		char temp[8],humid[8],press[12];
		float_to_str(temp,t);
		float_to_str(humid,h);
		float_to_str(press,p);
		//log_info(TAG,"td=%g,hd=%g,pd=%g",t,h,p);
		log_info(TAG,"td=%s,hd=%s,pd=%s",temp,humid,press);
#ifdef CONFIG_INFLUX
		char buf[64];
		if (sizeof(buf) > snprintf(buf,sizeof(buf),"temperature=%s,humidity=%s,pressure=%s",temp,humid,press))
			influx_send(buf);
#endif
#ifdef CONFIG_MQTT
		strcat(temp,"\u00b0C");
		mqtt_publish("temperature",temp,strlen(temp),0);
		strcat(temp,"%");
		mqtt_publish("humidity",humid,strlen(humid),0);
		strcat(temp,"Pa");
		mqtt_publish("pressure",press,strlen(humid),0);
#endif
	}
	return Interval;
}


extern "C"
void bme_setup(void)
{
	if (int i = bme280_init(CONFIG_BME280_SDA,CONFIG_BME280_SCL)) {
		log_error(TAG,"init failed: %d",i);
		return;
	}
	add_cyclic_task("bme",gatherData);
	Enabled = true;
	Initialized = true;
}


int bme(Terminal &term, int argc, const char *args[])
{
	if (argc > 3) {
		term.printf("%s: 0-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 3) {
		if (0 == strcmp(args[1],"interval")) {
			long l = strtol(args[2],0,0);
			if (l < 1000) {
				term.printf("minimum value is 1000");
				return 1;
			}
			Interval = l;
			return 0;
		}
		term.printf("invalid argument\n");
		return 1;
	}
	if (argc == 2) {
		if (!strcmp(args[1],"enable"))
			Enabled = true;
		else if (!strcmp(args[1],"disable"))
			Enabled = false;
		else if (!strcmp(args[1],"sample"))
			gatherData();
		else if (!strcmp(args[1],"-h"))
			term.printf("valid arguments are <none>,sample,enable,disable\n");
		else {
			term.printf("invalid argument\n");
			return 1;
		}
		return 0;
	}
#ifdef ESP8266
	char buf[12];
	if (RTData.has_temperature())
		term.printf("temperature: %s\u0b0C\n",float_to_str(buf,RTData.temperature()));
	if (RTData.has_humidity())
		term.printf("humidity   : %s%\n",float_to_str(buf,RTData.humidity()));
	if (RTData.has_pressure())
		term.printf("pressure   : %sPa\n",float_to_str(buf,RTData.pressure()));
#else
	if (RTData.has_temperature())
		term.printf("temperature: %gC\n",(double) RTData.temperature());
	if (RTData.has_humidity())
		term.printf("humidity   : %g%\n",(double) RTData.humidity());
	if (RTData.has_pressure())
		term.printf("pressure   : %gPa\n",(double) RTData.pressure());
#endif
	return 0;
}

#endif
