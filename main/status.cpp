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

#ifdef CONFIG_STATUSLEDS

#include "actions.h"
#include "alarms.h"
#include "binformats.h"
#include "button.h"
#include "globals.h"
#include "log.h"
#include "shell.h"
#include "status.h"
#include "terminal.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <string.h>

#if defined CONFIG_IDF_TARGET_ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif


static char TAG[] = "status";

static uint16_t OnTime[] = {
	/* auto */	0,
	/* off */	0,
	/* on */	1000,
	/* seldom */	70,
	/* often */	70,
	/* -seldom */	1800,
	/* -often */	500,
	/* heartbeat */	150,
};


static uint16_t OffTime[] = {
	/* auto */	0,
	/* off */	1000,
	/* on */	0,
	/* seldom */	1800,
	/* often */	500,
	/* -seldom */	70,
	/* -often */	70,
	/* heartbeat */	400,
};


static const char *ModeNames[] = {
	"auto",
	"off",
	"on",
	"seldom",
	"often",
	"neg-seldom",
	"neg-often",
	"heartbeat",
};

static uint16_t LedMode, CurMode;
static uint32_t PressTime = 0;
static bool Status = false;


void statusled_set(uint16_t l)
{
	log_dbug(TAG,"set mode %d",(int)l);
	LedMode = l;
}


uint16_t statusled_get()
{
	return LedMode;
}


static void button_press_callback(void *)
{
	PressTime = esp_timer_get_time()/1000;
}


static void button_rel_callback(void *)
{
	PressTime = 0;
}


static unsigned update_mode()
{
	if (LedMode) {
		if (LedMode == CurMode)
			return 0;
		CurMode = LedMode;
		return 1;
	}
	uint16_t l;
	uint32_t now = esp_timer_get_time()/1000;
	bool t = alarms_enabled();
	if (PressTime) {
		uint32_t dt = now - PressTime;
		if (dt > BUTTON_LONG_START*2)
			l = LedMode;
		else if (dt > BUTTON_LONG_START)
			l = 150;
		else if (dt > BUTTON_MED_START)
			l = 400;
		else
			l = LedMode;
	} else if (t && (StationMode == station_connected))
		l = ledmode_on;
	else if (!t && (StationMode == station_connected))
		l = ledmode_neg_seldom;
	else if (StationMode == station_starting)
		l = 200;
	else if (StationMode == station_disconnected)
		l = 100;
	else if (t && (StationMode == station_stopped))
		l = ledmode_pulse_seldom;
	else
		l = 1000;
	if (l != CurMode) {
		if (l < ledmode_max)
			log_info(TAG,"switching to %s",ModeNames[l]);
		else
			log_info(TAG,"switching to blink %ums",l);
		CurMode = l;
		return 1;
	}
	return 0;
}


static void delay(uint32_t d)
{
#define CHECK_INTERVAL 100
	while (d > CHECK_INTERVAL) {
		vTaskDelay(CHECK_INTERVAL/portTICK_PERIOD_MS);
		if (update_mode())
			return;
		d -= CHECK_INTERVAL;
	}
	vTaskDelay(d/portTICK_PERIOD_MS);
	update_mode();
}


void gpio_low(void *arg)
{
	gpio_num_t gpio = (gpio_num_t)(int)arg;
	gpio_set_level(gpio, 0);
}


void gpio_high(void *arg)
{
	gpio_num_t gpio = (gpio_num_t)(int)arg;
	gpio_set_level(gpio, 1);
}


extern "C"
void status_task(void *param)
{
	const LedConfig *c = (const LedConfig *)param;
	gpio_num_t gpio = (gpio_num_t) c->gpio();
	uint8_t on = c->config() & 1;
	Status = true;
	log_info(TAG,"ready");
	while(1) {
		if (uint16_t d_on = CurMode < ledmode_max ? OnTime[CurMode] : CurMode) {
			gpio_set_level(gpio, on);
			delay(d_on);
		}
		if (uint16_t d_off = CurMode < ledmode_max ? OffTime[CurMode] : CurMode) {
			gpio_set_level(gpio, on^1);
			delay(d_off);
		}
		if (uint16_t d_on = CurMode < ledmode_max ? OnTime[CurMode] : CurMode) {
			gpio_set_level(gpio, on);
			delay(d_on);
		}
		if (uint16_t d_off = CurMode < ledmode_max ? OffTime[CurMode] : CurMode) {
			gpio_set_level(gpio, on^1);
			delay(d_off);
		}
		if (CurMode == ledmode_heartbeat) {
			delay(500);
		}
	}
}


extern "C"
void heartbeat_task(void *param)
{
	const LedConfig *c = (LedConfig *)param;
	gpio_num_t gpio = (gpio_num_t) c->gpio();
	uint8_t on = c->config() & 1;
	if (esp_err_t e = gpio_set_direction(gpio,GPIO_MODE_OUTPUT)) {
		log_error(TAG,"gpio %d set direction: %s",(int)gpio,esp_err_to_name(e));
		return;
	}
	while(1) {
		gpio_set_level(gpio,on);
		vTaskDelay(100/portTICK_PERIOD_MS);
		gpio_set_level(gpio,on^1);
		vTaskDelay(300/portTICK_PERIOD_MS);
		gpio_set_level(gpio,on);
		vTaskDelay(100/portTICK_PERIOD_MS);
		gpio_set_level(gpio,on^1);
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}


int status(Terminal &term, int argc, const char *args[])
{
	/*
	assert(sizeof(OnTime) == sizeof(OffTime));
	for (size_t i = 0; i < sizeof(OnTime)/sizeof(OnTime[0]); ++i)
		assert((OnTime[i] != 0) || (OffTime[i] != 0));
	*/
	if (!Status) {
		term.println("no statusled defined");
		return 1;
	}
	if (argc > 2)
		return arg_invnum(term);
	if (argc == 1) {
		if (LedMode < sizeof(ModeNames)/sizeof(ModeNames[0]))
			term.printf("%s mode %s\n",CurMode == ledmode_auto ? "auto" : "manual",ModeNames[LedMode]);
		else
			term.printf("fixed interval %dms",CurMode);
		return 0;
	}
	if (0 == strcmp(args[1],"-l")) {
		for (size_t i = 0; i < sizeof(ModeNames)/sizeof(ModeNames[0]); ++i)
			term.println(ModeNames[i]);
		return 0;
	}
	if (0 == strcmp(args[1],"auto")) {
		LedMode = ledmode_auto;
		return 0;
	}
	long l = strtol(args[1],0,0);
	if ((l > ledmode_max) && (l <= UINT16_MAX)) {
		LedMode = l;
		return 0;
	}
	for (size_t i = 0; i < sizeof(ModeNames)/sizeof(ModeNames[0]); ++i) {
		if (!strcmp(args[1],ModeNames[i])) {
			LedMode = i;
			return 0;
		}
	}
	return arg_invalid(term,args[1]);
}

#ifdef CONFIG_IDF_TARGET_ESP32
#define stacksize 2560
#else
#define stacksize 1536
#endif

int status_setup()
{
	for (const LedConfig &c : HWConf.led()) {
		if ((c.pwm_ch() != -1) || (c.gpio() == -1))	// these are handled in the dimmer
			continue;
		const char *n = c.name().c_str();
		gpio_num_t gpio = (gpio_num_t) c.gpio();
		gpio_pad_select_gpio(gpio);
		gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
		if (0 == strcmp(n,"heartbeat")) {
			BaseType_t r = xTaskCreatePinnedToCore(heartbeat_task, "heartbeat", stacksize, (void*)&c, 1, NULL, APP_CPU_NUM);
			if (r != pdPASS) {
				log_error(TAG,"heartbeat task creation failed: %s",esp_err_to_name(r));
				return 1;
			}
			log_info(TAG,"heartbeat started");
		} else if (0 == strcmp(n,"status")) {
			LedMode = (uint16_t)ledmode_auto;
			CurMode = (uint16_t)ledmode_off;
			BaseType_t r = xTaskCreatePinnedToCore(status_task, "status", stacksize, (void*)&c, 21, NULL, APP_CPU_NUM);
			if (r != pdPASS) {
				log_error(TAG,"status task creation failed: %s",esp_err_to_name(r));
				return 1;
			}
		} else {
			uint8_t on = c.config() & 1;
			action_add(concat(n,"!on"),on ? gpio_high : gpio_low,(void*)gpio,"turn led on");
			action_add(concat(n,"!off"),on ? gpio_low : gpio_high,(void*)gpio,"turn led off");
		}
		action_add("statusled!btnpress",button_press_callback,0,"bind to button press event that should be monitored by status LED");
		action_add("statusled!btnrel",button_rel_callback,0,"bind to button release event that should be monitored by status LED");
	}
	return 0;
}

#endif
