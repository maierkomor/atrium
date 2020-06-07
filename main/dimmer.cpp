/*
 *  Copyright (C) 2019, Thomas Maier-Komor
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

#ifdef CONFIG_DIMMER_GPIO

#include <errno.h>
#include <stddef.h>

#ifdef ESP8266
#include <driver/pwm.h>
#elif defined ESP32
#include <driver/ledc.h>
#define SPEED_MODE LEDC_LOW_SPEED_MODE
#else
#error missing implementation
#endif

#include "actions.h"
#include "cyclic.h"
#include "globals.h"
#include "dimmer.h"
#include "terminal.h"
#include "log.h"
#include "mqtt.h"


static char TAG[] = "dim";
static uint16_t DimSet, DimCur;


int dimmer_set_value(unsigned v)
{
	if (v > DIM_MAX)
		return EINVAL;
	DimSet = v;
	char tmp[16];
	int n = sprintf(tmp,"%u",(unsigned)v);
#ifdef CONFIG_MQTT
	mqtt_publish("dimmer",tmp,n,1);
#endif
	return 0;
}


unsigned dimmer_get_value()
{
	return DimSet;
}


unsigned dimmer_fade()
{
	auto cur = DimCur;
	if (cur == DimSet)
		return 500;
	if (DimSet > cur)
		++cur;
	else
		--cur;
	DimCur = cur;
#ifndef CONFIG_DIMMER_ACTIVE_HIGH
	cur = DIM_MAX - cur;
#endif
#ifdef ESP8266
	pwm_set_duty(0,cur);
	pwm_start();
#elif defined ESP32
	//ledc_set_fade_time_and_start(SPEED_MODE,LEDC_CHANNEL_0,v,1000,LEDC_FADE_NO_WAIT);
	ledc_set_duty_and_update(SPEED_MODE,LEDC_CHANNEL_0,cur,DIM_MAX);
#else
#error missing implementation
#endif
	return Config.dim_step();
}


int dim(Terminal &t, int argc, const char *argv[])
{
	if (argc == 1) {
		t.printf("set: %u\ncurrent: %u\n",(unsigned)DimSet,(unsigned)DimCur);
		return 0;
	}
	if (argc > 2) {
		t.printf("too many arguments\n");
		return 1;
	}
	long l = strtol(argv[1],0,0);
	if ((l < 0) || (l > DIM_MAX)) {
		t.printf("invalid argument - valid range: 0-%u\n",DIM_MAX);
		return 1;
	}
	return dimmer_set_value(l);
}


#ifdef CONFIG_AT_ACTIONS
static void dimmer_on()
{
	dimmer_set_value(DIM_MAX);
}


static void dimmer_off()
{
	dimmer_set_value(0);
}
#endif


int dimmer_setup()
{
	log_info(TAG,"setup");
#ifdef CONFIG_DIMMER_START
	DimSet = DIM_MAX;
#else
	DimSet = 0;
#endif
	DimCur = 0;

#ifdef ESP8266
	uint32_t pins[1] = {CONFIG_DIMMER_GPIO};
	uint32_t duties[1] = {DimSet};
	int16_t phases[1] = {0};
	if (esp_err_t e = pwm_init(DIM_MAX,duties,1,pins)) {
		log_error(TAG,"pwm_init %x",e);
		return e;
	}
	pwm_set_phases(phases);
	pwm_start();
#elif defined ESP32
	ledc_timer_config_t tm;
	tm.duty_resolution = LEDC_TIMER_8_BIT;
	tm.freq_hz         = 1000;
	tm.speed_mode      = SPEED_MODE;
	tm.timer_num       = LEDC_TIMER_0;
	if (esp_err_t e = ledc_timer_config(&tm)) {
		log_error(TAG,"timer config %x",e);
		return e;
	}

	ledc_channel_config_t ch;
	ch.channel    = LEDC_CHANNEL_0;
	ch.duty       = DimCur;
	ch.gpio_num   = CONFIG_DIMMER_GPIO;
	ch.speed_mode = SPEED_MODE;
	ch.timer_sel  = LEDC_TIMER_0;
	ch.hpoint     = DIM_MAX;
	ch.intr_type  = LEDC_INTR_DISABLE;
	if (esp_err_t e = ledc_channel_config(&ch)) {
		log_error(TAG,"channel config %x",e);
		return e;
	}
	if (esp_err_t e = ledc_fade_func_install(0)) {
		log_error(TAG,"fade install %x",e);
		return e;
	}
#else
#error missing implementation
#endif
#ifdef CONFIG_AT_ACTIONS
	add_action("dimmer_on",dimmer_on,"turn on with PWM ramp");
	add_action("dimmer_off",dimmer_off,"turn on with PWM ramp");
#endif
	add_cyclic_task("dimmer",dimmer_fade,0);
	log_info(TAG,"running");
	return 0;
}


#endif
