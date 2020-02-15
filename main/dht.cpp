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

#include "cyclic.h"
#include "dht.h"
#include "dhtdrv.h"
#include "globals.h"
#include "influx.h"
#include "log.h"
#include "mqtt.h"
#include "support.h"
#include "terminal.h"

#include <driver/gpio.h>

#ifdef CONFIG_DHT11
#define DHT_MODEL 11
#elif defined  CONFIG_DHT21
#define DHT_MODEL 21
#elif defined  CONFIG_DHT22
#define DHT_MODEL 22
#else
#error unknown DHT model
#endif

static DHT *Dht = 0;
static char TAG[] = "DHT";
static bool Enabled = false;
static unsigned Interval;


/*
int16_t dht_temperature()
{
	if (Dht == 0)
		return INT16_MIN;
	return Dht->getRawTemperature();
}


uint8_t dht_humidity()
{
	if (Dht == 0)
		return UINT8_MAX;
	return Dht->getRawHumidity();
}
*/


int dht_init()
{
	if (Dht != 0) {
		delete Dht;
		Dht = 0;
	}
	Interval = 60000;
	Dht = new DHT(CONFIG_DHT_GPIO,(DHTModel_t)DHT_MODEL);
	Enabled = Dht->begin();
	return Enabled ? 1 : 0;
}


static unsigned gatherDHTdata()
{
	if (Dht == 0)
		return 300000;
	if (!Enabled)
		return Interval;
	Dht->read();
	if (const char *err = Dht->getError()) {
		log_info(TAG,"driver error: %s",err);
		RTData.clear_temperature();
		RTData.clear_humidity();
	} else {
		char temp[8],humid[8];
		float_to_str(temp,Dht->getTemperature());
		log_info(TAG,"temperature %sC",temp);
#ifdef CONFIG_MQTT
		mqtt_publish("temperature",temp,strlen(temp),0);
#endif
		float_to_str(humid,Dht->getHumidity());
		log_info(TAG,"humidity %s%%",humid);
#ifdef CONFIG_MQTT
		mqtt_publish("humidity",humid,strlen(humid),0);
#endif
		RTData.set_temperature(Dht->getTemperature());
		RTData.set_humidity(Dht->getHumidity());
#ifdef CONFIG_INFLUX
		char buf[128];
		if (sizeof(buf) > snprintf(buf,sizeof(buf),"temperature=%s,humidity=%s",temp,humid))
			influx_send(buf);
#endif
	}
	return Interval;
}


extern "C"
void dht_setup(void)
{
	if (0 == dht_init())
		return;
	add_cyclic_task("dht",gatherDHTdata);
}


int dht(Terminal &term, int argc, const char *args[])
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
			gatherDHTdata();
		else if (!strcmp(args[1],"-h"))
			term.printf("valid arguments are <none>,sample,enable,disable\n");
		else {
			term.printf("invalid argument\n");
			return 1;
		}
		return 0;
	}
	if (Dht == 0) {
		term.printf("dht not intialized\n");
	} else if (const char *e = Dht->getError()) {
		term.printf("dht error: %s\n",e);
	} else {
#ifdef ESP8266
		char buf[8];
		term.printf("dht temperature: %sC\n",float_to_str(buf,Dht->getTemperature()));
		term.printf("dht humidity   : %s%\n",float_to_str(buf,Dht->getHumidity()));
#else
		term.printf("dht temperature: %gC\n",(double) Dht->getTemperature());
		term.printf("dht humidity   : %g%\n",(double) Dht->getHumidity());
#endif
	}
	return 0;
}

#endif
