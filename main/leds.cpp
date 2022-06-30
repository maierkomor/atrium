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

#ifdef CONFIG_LEDS

#include "actions.h"
#include "alarms.h"
#include "button.h"
#include "cyclic.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "leds.h"
#include "log.h"
#include "terminal.h"
#include "wifi.h"
#include "xio.h"

#include <string.h>

#if defined CONFIG_IDF_TARGET_ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif


#define TAG MODULE_LED

struct LedMode
{
	LedMode(const char *n, ledmode_t m, xio_t g, bool o);

	int set_mode(const char *m);

	LedMode *next = 0;
	const char *name;
	uint32_t update = 0;
	ledmode_t mode;
	int8_t state;
	xio_t gpio;
	bool on;
	static LedMode *First;
};

LedMode *LedMode::First = 0, *Status = 0;

static uint32_t PressTime = 0;

// 0: automatic
// < 0: rewind x steps
// > 0: bit 0: on/off, bit 6-1: x*10ms
static const int8_t Modes[] = {
#define MODEOFF_AUTO 0
	0,		// mode=0 => not an auto-mode
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
#define MODEOFF_ONCE 41
	(20 << 1) | 1,	// 200ms on
	-42,		// goto off
#define MODEOFF_TWICE 43
	(15 << 1) | 1,	// 150ms on
	(10 << 1) | 0,	// 100ms off
	(15 << 1) | 1,	// 150ms on
	-46,		// goto off
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
	MODEOFF_ONCE,
	MODEOFF_TWICE,
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
	"once",
	"twice",
};


LedMode::LedMode(const char *n, ledmode_t m, xio_t g, bool o)
: next(First)
, name(strdup(n))
, mode(m)
, state(ModeOffset[m])
, gpio(g)
, on(o)
{
	First = this;
}


int LedMode::set_mode(const char *m)
{
	for (int i = 0; i < sizeof(ModeNames)/sizeof(ModeNames[0]); ++i) {
		if (0 == strcmp(ModeNames[i],m)) {
			mode = (ledmode_t)i;
			state = ModeOffset[i];
			update = 0;
			return 0;
		}
	}
	log_warn(TAG,"invalid mode %s",m);
	return 1;
}


static void update_mode(LedMode *ctx, uint32_t now)
{
	if (ctx->mode == ledmode_auto)
		return;
	ledmode_t l = ctx->mode;
	if (PressTime) {
		uint32_t dt = now - PressTime;
		if (dt > BUTTON_LONG_START*2)
			l = ledmode_on;
		else if (dt > BUTTON_LONG_START)
			l = ledmode_fast;
		else if (dt > BUTTON_MED_START)
			l = ledmode_medium;
		else
			l = ctx->mode;
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
	if (l != ctx->mode) {
		log_dbug(TAG,"switching to %s",ModeNames[l]);
		ctx->mode = l;
		ctx->state = ModeOffset[l];
		ctx->update = 0;
	}
}


static unsigned ledmode_subtask(void *arg)
{
	LedMode *ctx = (LedMode *)arg;
	uint32_t now = esp_timer_get_time()/1000;
	if ((ctx == Status) && (ctx->mode))
		update_mode(ctx,now);
	if (now < ctx->update)
		return now - ctx->update;
	int state = ctx->state;
	int8_t d = Modes[state];
	if (d < 0) {
		state += d;
		assert(state >= 0);
		ctx->state = state;
		d = Modes[state];
	}
	xio_set_lvl(ctx->gpio, (xio_lvl_t)(ctx->on == (d&1)));
	unsigned dt = (d>>1) * 10;
	ctx->update = now + dt;
	++ctx->state;
	log_dbug(TAG,"led %s: mode %d, delay %d, update %u, state %u, %s",ctx->name,ctx->mode,d>>1,ctx->update,ctx->state,d&1?"on":"off");
	return dt > 50 ? 50 : dt;
}


static void led_set_mode(void *arg)
{
	const char *a = (const char *) arg;
	char *sp = strchr(a,':');
	if (sp == 0) {
		log_warn(TAG,"led!set invalid arg '%s'",a);
		return;
	}
	char name[sp-a+1];
	memcpy(name,a,sp-a);
	name[sp-a] = 0;
	LedMode *m = LedMode::First;
	++sp;
	log_dbug(TAG,"set %s to %s",name,sp);
	while (m) {
		if (0 == strcmp(m->name,name)) {
			m->set_mode(sp);
			return;
		}
		m = m->next;
	}
	log_warn(TAG,"led!set invalid arg '%s'",a);
}


static void button_rel_callback(void*)
{
	PressTime = 0;
}


static void button_press_callback(void*)
{
	PressTime = esp_timer_get_time()/1000;
}


static void led_set_on(void *arg)
{
	LedMode *m = (LedMode *)arg;
	if (m) {
		m->mode = ledmode_on;
		m->state = ModeOffset[ledmode_on];
		m->update = 0;
	}
}


static void led_set_off(void *arg)
{
	LedMode *m = (LedMode *)arg;
	if (m) {
		m->mode = ledmode_off;
		m->state = ModeOffset[ledmode_off];
		m->update = 0;
	}
}


extern "C"
void statusled_set(ledmode_t m)
{
	if (Status) {
		if (m == ledmode_auto) {
			Status->mode = ledmode_on;
			Status->state = MODEOFF_ON;
		} else {
			Status->mode = ledmode_auto;
			Status->state = ModeOffset[m];
		}
	}
}


int led_set(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		LedMode *m = LedMode::First;
		while (m) {
			term.printf("%-12s %s\n",m->name,ModeNames[m->mode]);
			m = m->next;
		}
		return 0;
	}
	if (0 == strcmp(args[1],"-l")) {
		for (const char *mn : ModeNames)
			term.println(mn);
		return 0;
	}
	LedMode *m = LedMode::First;
	while (m && strcmp(m->name,args[1])) {
		m = m->next;
	}
	if (m == 0)
		return arg_invalid(term,args[1]);
	if (argc == 2) {
		term.printf("%-12s %s\n",m->name,ModeNames[m->mode]);
		return 0;
	} else if (argc == 3) {
		if (m->set_mode(args[2]))
			return arg_invalid(term,args[2]);
		return 0;
	}
	/*
	if (argc == 3) {
		if (m->mode < sizeof(ModeNames)/sizeof(ModeNames[0]))
			term.printf("%s mode %s\n",Status->mode != 0 ? "auto" : "manual",ModeNames[Status->mode]);
		else
			term.printf("invalid mode %d\n",Status->mode);
		return 0;
	}
	if (0 == strcmp(args[2],"auto")) {
		Status->mode = ledmode_on;
		Status->state = MODEOFF_ON;
		return 0;
	}
	for (size_t i = 0; i < sizeof(ModeNames)/sizeof(ModeNames[0]); ++i) {
		if (!strcmp(args[1],ModeNames[i])) {
			Status->state = ModeOffset[i];
			Status->mode = ledmode_auto;
			return 0;
		}
	}
	*/
	return arg_invalid(term,args[1]);
}

#ifdef CONFIG_IDF_TARGET_ESP32
#define stacksize 2560
#else
#define stacksize 1536
#endif

int leds_setup()
{
	bool have_st = false;
	unsigned numled = 0;
	for (const LedConfig &c : HWConf.led()) {
		if ((c.pwm_ch() != -1) || (c.gpio() == -1))	// these are handled in the dimmer
			continue;
		const auto &n = c.name();
		const char *name = n.c_str();
		xio_cfg_t cfg = XIOCFG_INIT;
		cfg.cfg_io = c.config_open_drain() ? xio_cfg_io_od : xio_cfg_io_out;
		xio_t gpio = (xio_t) c.gpio();
		if (0 > xio_config(gpio,cfg)) {
			log_warn(TAG,"failed to configure xio%u",gpio);
			continue;
		}
		uint8_t on = c.config_active_high();
		LedMode *ctx;
		if (n == "heartbeat") {
			ctx = new LedMode(name,ledmode_heartbeat,gpio,on);
			cyclic_add_task("heartbeat",ledmode_subtask,(void*)ctx);
		} else if (n == "status") {
			if (have_st) {
				log_warn(TAG,"multiple status led");
				continue;
			}
			have_st = true;
			ctx = new LedMode(name,ledmode_on,gpio,on);
			Status = ctx;
			action_add("statusled!btnpress",button_press_callback,0,"bind to button press event to monitor with status LED");
			action_add("statusled!btnrel",button_rel_callback,0,"bind to button release event to monitor with status LED");
		} else {
			ctx = new LedMode(name,ledmode_off,gpio,on);
			action_add(concat(name,"!on"), led_set_on, (void*)ctx, "led on");
			action_add(concat(name,"!off"), led_set_off, (void*)ctx, "led off");
		}
		cyclic_add_task(name,ledmode_subtask,(void*)ctx);
		log_info(TAG,"added %s at %u",name,gpio);
		++numled;
	}
	if (numled)
		action_add("led!set",led_set_mode,0,"set LED mode (on,off,slow,fast,once,twice)");
	return 0;
}

#endif
