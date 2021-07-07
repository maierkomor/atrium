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

#ifdef CONFIG_STATUSLEDS

#include "actions.h"
#include "alarms.h"
#include "button.h"
#include "cyclic.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
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


static const char TAG[] = "status";

struct StatusCtx
{
	uint32_t update;
	int8_t mode;
	ledmode_t basemode;
	gpio_num_t gpio;
	bool on;
};

static uint32_t PressTime = 0;
static StatusCtx *Status = 0;

// 0: automatic
// < 0: rewind x steps
// > 0: bit 0: on/off, bit 6-1: x*10ms
static const int8_t Modes[] = {
#define MODEOFF_AUTO 0
	0,		// basemode=0 => not an auto-mode
#define MODEOFF_OFF 1
	(63 << 1) | 0,	// 630ms off
	-1,
#define MODEOFF_ON 3
	(63 << 1) | 1,	// 630ms on
	-1,
#define MODEOFF_SELDOM 5
	(7 << 1) | 1,	// 70ms on
	(60 << 1) | 0,	// 600ms off
	(60 << 1) | 0,	// 600ms off
	(60 << 1) | 0,	// 600ms off
	-4,
#define MODEOFF_OFTEN 10
	(7 << 1) | 1,	// 70ms on
	(50 << 1) | 0,	// 500ms off
	-2,
#define MODEOFF_NSELDOM 13
	(7 << 1) | 0,	// 70ms off
	(60 << 1) | 1,	// 600ms on
	(60 << 1) | 1,	// 600ms on
	(60 << 1) | 1,	// 600ms on
	-4,
#define MODEOFF_NOFTEN 18
	(7 << 1) | 0,	// 70ms off
	(50 << 1) | 1,	// 500ms on
	-2,
#define MODEOFF_HEARTBEAT 21
	(15 << 1) | 1,	// 150ms on
	(40 << 1) | 0,	// 400ms off
	(15 << 1) | 1,	// 150ms on
	(40 << 1) | 0,	// 400ms off
	(40 << 1) | 0,	// 400ms off
	-5,
#define MODEOFF_SLOW 27
	(50 << 1) | 1,	// 500ms on
	(50 << 1) | 1,	// 500ms on
	(50 << 1) | 0,	// 500ms off
	(50 << 1) | 0,	// 500ms off
	-4,
#define MODEOFF_MEDIUM 32
	(40 << 1) | 1,	// 400ms on
	(40 << 1) | 0,	// 400ms off
	-2,
#define MODEOFF_FAST 35
	(10 << 1) | 1,	// 100ms on
	(10 << 1) | 0,	// 100ms off
	-2,
#define MODEOFF_VFAST 38
	(5 << 1) | 1,	// 50ms on
	(5 << 1) | 0,	// 50ms off
	-2,
};


static const uint8_t ModeOffset[] = {
	MODEOFF_AUTO,
	MODEOFF_OFF,
	MODEOFF_ON,
	MODEOFF_SELDOM,
	MODEOFF_OFTEN,
	MODEOFF_NSELDOM,
	MODEOFF_NOFTEN,
	MODEOFF_HEARTBEAT,
	MODEOFF_SLOW,
	MODEOFF_MEDIUM,
	MODEOFF_FAST,
	MODEOFF_VFAST,
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
	"slow",
	"medium",
	"fast",
	"very-fast",
};


static void update_mode(StatusCtx *ctx, uint32_t now)
{
	if (ctx->basemode == ledmode_auto)
		return;
	ledmode_t l = ctx->basemode;
	if (PressTime) {
		uint32_t dt = now - PressTime;
		if (dt > BUTTON_LONG_START*2)
			l = ledmode_on;
		else if (dt > BUTTON_LONG_START)
			l = ledmode_fast;
		else if (dt > BUTTON_MED_START)
			l = ledmode_medium;
		else
			l = ctx->basemode;
	} else if (StationMode == station_connected)
		l = alarms_enabled() ? ledmode_off : ledmode_pulse_seldom;
	else if (StationMode == station_starting)
		l = ledmode_medium;
	else if (StationMode == station_disconnected)
		l = ledmode_slow;
	else if (StationMode == station_stopped)
		l = alarms_enabled() ? ledmode_on : ledmode_neg_seldom;
	else
		abort();
	if (l != ctx->basemode) {
		log_dbug(TAG,"switching to %s",ModeNames[l]);
		ctx->basemode = l;
		ctx->mode = ModeOffset[l];
		ctx->update = 0;
	}
}


static unsigned status_subtask(void *arg)
{
	StatusCtx *ctx = (StatusCtx *)arg;
	uint32_t now = esp_timer_get_time()/1000;
	if (ctx->basemode)
		update_mode(ctx,now);
	if (now < ctx->update)
		return now - ctx->update;
	uint8_t mode = ctx->mode;
	int8_t d = Modes[mode];
	if (d < 0) {
		mode += d;
		ctx->mode = mode;
		d = Modes[mode];
	}
	gpio_set_level(ctx->gpio, ctx->on == (d&1));
	unsigned dt = (d>>1) * 10;
	ctx->update = now + dt;
	++ctx->mode;
	log_dbug(TAG,"mode 0x%x, update %u, newmode-offset %u",d,ctx->update,ctx->mode);
	return dt > 50 ? 50 : dt;
}


static void button_rel_callback(void*)
{
	PressTime = 0;
}


static void button_press_callback(void*)
{
	PressTime = esp_timer_get_time()/1000;
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


void statusled_set(ledmode_t m)
{
	if (Status == 0)
		return;
	if (m == ledmode_auto) {
		Status->basemode = ledmode_on;
		Status->mode = MODEOFF_ON;
	} else {
		Status->basemode = ledmode_auto;
		Status->mode = ModeOffset[m];
	}
}


int status(Terminal &term, int argc, const char *args[])
{
	if (Status == 0) {
		term.println("no statusled defined");
		return 1;
	}
	if (argc > 2)
		return arg_invnum(term);
	if (argc == 1) {
		if (Status->basemode < sizeof(ModeNames)/sizeof(ModeNames[0]))
			term.printf("%s mode %s\n",Status->basemode != 0 ? "auto" : "manual",ModeNames[Status->basemode]);
		else
			term.printf("invalid mode %d\n",Status->basemode);
		return 0;
	}
	if (0 == strcmp(args[1],"-l")) {
		for (size_t i = 0; i < sizeof(ModeNames)/sizeof(ModeNames[0]); ++i)
			term.println(ModeNames[i]);
		return 0;
	}
	if (0 == strcmp(args[1],"auto")) {
		Status->basemode = ledmode_on;
		Status->mode = MODEOFF_ON;
		return 0;
	}
	for (size_t i = 0; i < sizeof(ModeNames)/sizeof(ModeNames[0]); ++i) {
		if (!strcmp(args[1],ModeNames[i])) {
			Status->mode = ModeOffset[i];
			Status->basemode = ledmode_auto;
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
		uint8_t on = c.config() & 1;
		if (0 == strcmp(n,"heartbeat")) {
			StatusCtx *ctx = new StatusCtx;
			ctx->update = 0;
			ctx->gpio = gpio;
			ctx->mode = MODEOFF_HEARTBEAT;
			ctx->basemode = ledmode_auto;
			ctx->on = on;
			cyclic_add_task("heartbeat",status_subtask,(void*)ctx);
			log_dbug(TAG,"heartbeat started");
		} else if (0 == strcmp(n,"status")) {
			StatusCtx *ctx = new StatusCtx;
			ctx->update = 0;
			ctx->gpio = gpio;
			ctx->mode = MODEOFF_ON;
			ctx->basemode = ledmode_on;
			ctx->on = on;
			Status = ctx;
			cyclic_add_task("status",status_subtask,(void*)ctx);
			log_dbug(TAG,"status started");
		} else {
			action_add(concat(n,"!on"),on ? gpio_high : gpio_low,(void*)gpio,"turn led on");
			action_add(concat(n,"!off"),on ? gpio_low : gpio_high,(void*)gpio,"turn led off");
		}
		action_add("statusled!btnpress",button_press_callback,0,"bind to button press event to monitor with status LED");
		action_add("statusled!btnrel",button_rel_callback,0,"bind to button release event to monitor with status LED");
	}
	return 0;
}

#endif
