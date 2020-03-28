/*
 *  Copyright (C) 2019-2020, Thomas Maier-Komor
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

#ifdef CONFIG_LIGHTCTRL

#ifndef ESP8266
#error lightctrl is currntly only supported on ESP8266
#endif

#include <driver/adc.h>

#include "actions.h"
#include "cyclic.h"
#include "dimmer.h"
#include "globals.h"
#include "lightctrl.h"
#include "log.h"
#include "support.h"
#include "terminal.h"


#define	LED_MAX		DIM_MAX
#define LED_OFF		0

static char TAG[] = "light";
static uint16_t ADdata[8];
static uint8_t ADidx = 0;


#ifdef CONFIG_AT_ACTIONS
static void lightctrl_off()
{
	Config.set_lightctrl(false);
	dimmer_set_value(LED_OFF);
	log_info(TAG,"off");
}


static void lightctrl_on()
{
	Config.set_lightctrl(false);
	dimmer_set_value(LED_MAX);
	log_info(TAG,"on");
}


static void lightctrl_auto()
{
	Config.set_lightctrl(true);
	log_info(TAG,"auto");
}
#endif


unsigned lightctrl_measure()
{
	uint16_t adc;
	adc_read(&adc);
	ADdata[ADidx] = adc;
	++ADidx;
	ADidx %= sizeof(ADdata)/sizeof(ADdata[0]);
	float adc_f = 0;
	for (size_t i = 0; i < sizeof(ADdata)/sizeof(ADdata[0]); ++i)
		adc_f += ADdata[i];
	adc_f /= sizeof(ADdata)/sizeof(ADdata[0]);
	RTData.set_adc(adc_f);
	if (Config.lightctrl()) {
		auto v = dimmer_get_value();
		if (adc_f > (float)Config.threshold_off()) {
			log_info(TAG,"dim off");
			v = LED_OFF;
		} else if (adc_f < (float)Config.threshold_on()) {
			log_info(TAG,"dim on");
			v = LED_MAX;
		}
		dimmer_set_value(v);
	}
	return 1000;
}


void lightctrl_setup()
{
#ifdef CONFIG_AT_ACTIONS
	add_action("lightctrl_off",lightctrl_off,"disable lightctrl and turn off");
	add_action("lightctrl_on",lightctrl_on,"disable ligthctrl and turn on");
	add_action("lightctrl_auto",lightctrl_auto,"enable lightctrl");
#endif
#ifdef ESP32
	if (!Config.has_threshold_on())
		Config.set_threshold_on(5000);
	if (!Config.has_threshold_off())
		Config.set_threshold_off(55000);
#elif defined ESP8266
	if (!Config.has_threshold_on())
		Config.set_threshold_on(400);
	if (!Config.has_threshold_off())
		Config.set_threshold_off(800);
#endif
	if (!Config.has_dim_step())
		Config.set_dim_step(1000/DIM_MAX);	// full dim 1 sec
	adc_config_t c;
	c.mode = ADC_READ_TOUT_MODE;
	c.clk_div = 8;
	adc_init(&c);
	//add_cyclic_task("dimmer",lightctrl_dimmer);
	add_cyclic_task("lightctrl",lightctrl_measure);
}


int lightctrl(Terminal &t, int argc, const char *argv[])
{
	if (argc == 1) {
		t.printf("mode = %s\n",Config.lightctrl() ? "on" : "off");
		return 0;
	}
	if (argc != 2) {
		t.printf("synopsis: lightctrl [on|off]\n");
		return 1;
	}
	if (!strcmp(argv[1],"on")) {
		Config.set_lightctrl(true);
		return 0;
	}
	if (!strcmp(argv[1],"off")) {
		Config.set_lightctrl(false);
		return 0;
	}
	t.printf("valid arguments are: on, off\n");
	return 1;
}

#endif
