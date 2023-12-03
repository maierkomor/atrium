/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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


#ifdef CONFIG_BUZZER

#define TAG MODULE_BUZZER

#include "actions.h"
#include "buzzer.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "timefuse.h"
#include "terminal.h"

#include <driver/ledc.h>

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

// Buzzer is on LEDC_TIMER_1

/*
struct Tone
{
	uint16_t freq, duration;
};

struct Melody
{
	uint16_t num_tones;
	Tone tones[1];
};
*/

static timefuse_t Timer = 0;
static gpio_num_t BuzGpio;
static ledc_channel_t Channel;
static const Melody *Playing = 0;
static unsigned NextTone = 0;


void buzzer_start(unsigned freq, unsigned time)
{
	if (time == 0)
		return;
	log_dbug(TAG,"play tone %uHz, %ums",freq,time);
	if (freq > 0) {
		ledc_timer_config_t tm;
		bzero(&tm,sizeof(tm));
		tm.duty_resolution = LEDC_TIMER_8_BIT;
		tm.freq_hz         = freq;
		tm.speed_mode      = LEDC_LOW_SPEED_MODE;
		tm.timer_num       = LEDC_TIMER_1;
		tm.clk_cfg         = LEDC_AUTO_CLK;
		if (esp_err_t e = ledc_timer_config(&tm))
			log_warn(TAG,"buzzer timer config %x",e);
		else if (ledc_set_duty(LEDC_LOW_SPEED_MODE,Channel,128))
			log_warn(TAG,"set duty %x",e);
		else if (ledc_update_duty(LEDC_LOW_SPEED_MODE,Channel))
			log_warn(TAG,"update duty %x",e);
	} else {
		ledc_set_duty(LEDC_LOW_SPEED_MODE,Channel,0);
		ledc_update_duty(LEDC_LOW_SPEED_MODE,Channel);
	}
	timefuse_interval_set(Timer,time);
	if (timefuse_start(Timer))
		log_warn(TAG,"start timer failed");
}


static void buzzer_timeout(void *arg)
{
	if (Playing) {
		if (NextTone+2 <= Playing->tones_size()) {
			uint16_t freq = Playing->tones(NextTone++);
			uint16_t dur = Playing->tones(NextTone++);
			buzzer_start(freq,dur);
			return;
		}
		Playing = 0;
		NextTone = 0;
	}
	ledc_set_duty(LEDC_LOW_SPEED_MODE,Channel,0);
	ledc_update_duty(LEDC_LOW_SPEED_MODE,Channel);
}


static void buzzer_stop(void *arg)
{
	ledc_set_duty(LEDC_LOW_SPEED_MODE,Channel,0);
	ledc_update_duty(LEDC_LOW_SPEED_MODE,Channel);
	Playing = 0;
	NextTone = 0;
}


static void start_melody(const Melody &m)
{
	const auto &tones = m.tones();
	size_t nt = tones.size();
	if (nt < 2)
		return;
	if (nt >= 4) {
		Playing = &m;
		NextTone = 2;
	}
	buzzer_start(tones[0],tones[1]);
}


static const char *play_melody(const char *name)
{
	for (const auto &m : HWConf.buzzer().melodies()) {
		if (!strcmp(name,m.name().c_str())) {
			start_melody(m);
			return 0;
		}
	}
	return "Unknown melody.";
}


static void play_melody_action(void *arg)
{
	play_melody((const char *)arg);
}


const char *buzzer(Terminal &t, int argc, const char *args[])
{
	if (argc == 1) {
		for (const auto &m : HWConf.buzzer().melodies()) {
			t.printf("melody %s:\n   ",m.name().c_str());
			const auto &tones = m.tones();
			unsigned n = tones.size();
			unsigned i = 0;
			while (i+2 <= n) {
				t.printf(" [%ums @ %uHz]",tones[i+1],tones[i]);
				i += 2;
			}
			t.println();
		}
	} else if (argc == 2) {
		if (!strcmp("stop",args[1])) {
			buzzer_stop(0);
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 3) {
		if (!strcmp(args[1],"-p")) {
			return play_melody(args[2]);
		} else {
			char *e;
			long a1 = strtol(args[1],&e,0);
			if (*e)
				return "Invalid argument #1.";
			long a2 = strtol(args[2],&e,0);
			if (*e)
				return "Invalid argument #2.";
			buzzer_start(a1,a2);
		}
	} else if (!strcmp(args[1],"-a")) {
		if ((argc < 5) || ((argc & 1) == 0))
			return "Invalid number of arguments.";
		Melody *m = HWConf.mutable_buzzer()->add_melodies();
		m->set_name(args[2]);
		int n = 3;
		while (n + 1 < argc) {
			char *e;
			long t = strtol(args[n],&e,0);
			if (*e)
				return "Invalid frequency - incomplete melody.";
			long f = strtol(args[++n],&e,0);
			if (*e)
				return "Invalid time - incomplete melody.";
			m->add_tones(f);
			m->add_tones(t);
			++n;
		}
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


#ifdef CONFIG_LUA
static int luax_buzzer_tone(lua_State *L)
{
	int f = luaL_checkinteger(L,1);		// freq in Hz
	int t = luaL_checkinteger(L,2);		// time in ms
	buzzer_start(f,t);
	return 0;
}


static int luax_buzzer_play(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	play_melody(n);
	return 0;
}


static int luax_buzzer_stop(lua_State *L)
{
	buzzer_stop(0);
	return 0;
}


static LuaFn Functions[] = {
	{ "buzzer_tone", luax_buzzer_tone, "play buzzer with freq a0 Hz, and time a1 ms" },
	{ "buzzer_play", luax_buzzer_play, "play buzzer melody a0" },
	{ "buzzer_stop", luax_buzzer_stop, "stop buzzer" },
	{ 0, 0, 0 }
};
#endif


void buzzer_setup()
{
	if (!HWConf.has_buzzer() || !HWConf.buzzer().has_gpio())
		return;
	BuzGpio = (gpio_num_t)HWConf.buzzer().gpio();
	Timer = timefuse_create("buztmr",1000,false);
	Channel = (ledc_channel_t)(LEDC_CHANNEL_MAX-1);
//	dim->channel = (ledc_channel_t) conf.pwm_ch();
	ledc_channel_config_t ch;
	bzero(&ch,sizeof(ch));
	ch.gpio_num   = BuzGpio;
	ch.channel    = Channel;
	ch.duty       = 0;
	ch.speed_mode = LEDC_LOW_SPEED_MODE;
	ch.timer_sel  = LEDC_TIMER_1;
	ch.hpoint     = 0;
	ch.intr_type  = LEDC_INTR_DISABLE;
	if (esp_err_t e = ledc_channel_config(&ch))
		log_warn(TAG,"channel config %x",e);
	action_add("buzzer!play",play_melody_action,0,"play melody with buzzer");
	action_add("buzzer!stop",buzzer_stop,0,"stop buzzer");
	Action *a = action_add("buzzer!nexttone",buzzer_timeout,0,0);
	event_callback(timefuse_timeout_event(Timer),a);
	xlua_add_funcs("buzzer",Functions);
}


#endif	// BUZZER
