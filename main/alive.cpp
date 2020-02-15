/*
 *  Copyright (C) 2018-2019, Thomas Maier-Komor
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

#ifdef CONFIG_ALIVELED

#include "alive.h"
#include "globals.h"
#include "log.h"
#include "terminal.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <string.h>

#if defined ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif


#define CONFIG_ALIVELED_OFF (1^CONFIG_ALIVELED_ON)

#if CONFIG_ALIVELED_GPIO >= GPIO_PIN_COUNT
#error gpio value for alive LED is out of range
#endif


static char TAG[] = "alive";

static uint16_t OnTime[] = {
	/* off */	0,
	/* on */	1000,
	/* seldom */	70,
	/* often */	70,
	/* -seldom */	1800,
	/* -often */	500,
	/* heartbeat */	150,
};


static uint16_t OffTime[] = {
	/* off */	1000,
	/* on */	0,
	/* seldom */	1800,
	/* often */	500,
	/* -seldom */	70,
	/* -often */	70,
	/* heartbeat */	400,
};


static const char *ModeNames[] = {
	"off",
	"on",
	"seldom",
	"often",
	"neg-seldom",
	"neg-often",
	"heartbeat",
};

static uint16_t LedMode = (int)ledoff;
static bool Manual = false;


void set_aliveled(uint16_t l)
{
	LedMode = l;
}


uint16_t get_aliveled()
{
	return LedMode;
}


static void update_mode()
{
	if (Manual)
		return;
	uint16_t l;
	bool t = RTData.timers_enabled();
	if (t && (StationMode == station_connected))
		l = ledon;
	else if (!t && (StationMode == station_connected))
		l = neg_seldom;
	else if (StationMode == station_starting)
		l = 200;
	else if (StationMode == station_disconnected)
		l = 100;
	else if (t && (StationMode == station_stopped))
		l = pulse_seldom;
	else
		l = 1000;
	if (l != LedMode) {
		if (l < ledmode_max)
			log_info(TAG,"switching mode to %s",ModeNames[l]);
		else
			log_info(TAG,"switching mode to blink_%ums",l);
		LedMode = l;
	}
}


static void delay(uint32_t d)
{
#define CHECK_INTERVAL 500
	while (d > CHECK_INTERVAL) {
		vTaskDelay(CHECK_INTERVAL/portTICK_PERIOD_MS);
		update_mode();
		d -= CHECK_INTERVAL;
	}
	vTaskDelay(d/portTICK_PERIOD_MS);
	update_mode();
}


extern "C"
void alive_task(void *ignored)
{
	gpio_pad_select_gpio(CONFIG_ALIVELED_GPIO);
	gpio_set_direction((gpio_num_t)CONFIG_ALIVELED_GPIO, GPIO_MODE_OUTPUT);
	while(1) {
		if (uint16_t d_on = LedMode < ledmode_max ? OnTime[LedMode] : LedMode) {
			gpio_set_level((gpio_num_t)CONFIG_ALIVELED_GPIO, CONFIG_ALIVELED_ON);
			delay(d_on);
		}
		update_mode();
		if (uint16_t d_off = LedMode < ledmode_max ? OffTime[LedMode] : LedMode) {
			gpio_set_level((gpio_num_t)CONFIG_ALIVELED_GPIO, CONFIG_ALIVELED_OFF);
			delay(d_off);
		}
		update_mode();
		if (uint16_t d_on = LedMode < ledmode_max ? OnTime[LedMode] : LedMode) {
			gpio_set_level((gpio_num_t)CONFIG_ALIVELED_GPIO, CONFIG_ALIVELED_ON);
			delay(d_on);
		}
		update_mode();
		if (uint16_t d_off = LedMode < ledmode_max ? OffTime[LedMode] : LedMode) {
			gpio_set_level((gpio_num_t)CONFIG_ALIVELED_GPIO, CONFIG_ALIVELED_OFF);
			delay(d_off);
		}
		update_mode();
		if (LedMode == heartbeat) {
			delay(500);
			update_mode();
		}
	}
}


int alive(Terminal &term, int argc, const char *args[])
{
	assert(sizeof(OnTime) == sizeof(OffTime));
	for (size_t i = 0; i < sizeof(OnTime)/sizeof(OnTime[0]); ++i)
		assert((OnTime[i] != 0) || (OffTime[i] != 0));
	if (argc > 2) {
		term.printf("%s: 0-1 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		term.printf("current mode is %s\n",ModeNames[LedMode]);
		return 0;
	}
	if (0 == strcmp(args[1],"auto")) {
		Manual = false;
		return 0;
	}
	long l = strtol(args[1],0,0);
	if ((l > ledmode_max) && (l <= UINT16_MAX)) {
		LedMode = l;
		Manual = true;
		return 0;
	}
	for (size_t i = 0; i < sizeof(ModeNames)/sizeof(ModeNames[0]); ++i) {
		if (!strcmp(args[1],ModeNames[i])) {
			LedMode = i;
			Manual = true;
			return 0;
		}
	}
	term.printf("possbile modes are:\n	auto\n");
	for (size_t i = 0; i < sizeof(ModeNames)/sizeof(ModeNames[0]); ++i)
		term.printf("\t%s\n",ModeNames[i]);
	return 1;
}

#endif
