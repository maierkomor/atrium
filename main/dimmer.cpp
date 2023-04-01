/*
 *  Copyright (C) 2019-2022, Thomas Maier-Komor
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

#ifdef CONFIG_DIMMER

#include <errno.h>
#include <stddef.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <driver/pwm.h>
#elif defined CONFIG_IDF_TARGET_ESP32
#include <driver/ledc.h>
#define SPEED_MODE LEDC_HIGH_SPEED_MODE
#elif defined CONFIG_IDF_TARGET_ESP32S2
#include <driver/ledc.h>
#define SPEED_MODE LEDC_LOW_SPEED_MODE
#elif defined CONFIG_IDF_TARGET_ESP32S3
#include <driver/ledc.h>
#define SPEED_MODE LEDC_LOW_SPEED_MODE
#elif defined CONFIG_IDF_TARGET_ESP32C3
#include <driver/ledc.h>
#define SPEED_MODE LEDC_LOW_SPEED_MODE
#else
#error missing implementation
#endif
#include <driver/gpio.h>
#include <nvs.h>

#include "actions.h"
#include "cyclic.h"
#include "env.h"
#include "event.h"
#include "globals.h"
#include "dimmer.h"
#include "hwcfg.h"
#include "log.h"
#include "nvm.h"
#include "mqtt.h"
#include "settings.h"
#include "support.h"
#include "swcfg.h"
#include "terminal.h"

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif


#ifdef CONFIG_IDF_TARGET_ESP8266
#define DIM_MAX Period
#define PWM_BITS 16
#define DIM_INC	16
#define DIM_STEP 32
typedef uint8_t ledc_channel_t;
#else
#define DIM_MAX 1023
#define PWM_BITS 10
#define DIM_INC	4
#define DIM_STEP 8
#endif

#if PWM_BITS <= 8
typedef uint8_t duty_t;
#elif PWM_BITS <= 16
typedef uint16_t duty_t;
#elif PWM_BITS <= 32
typedef uint32_t duty_t;
#endif


#define TAG MODULE_DIM

unsigned Period = 1000;

struct Dimmer
{
	Dimmer *next;
	const char *name;
	EnvNumber *env;
	duty_t duty;
	float backup = 0;
	bool invert;
	gpio_num_t gpio;
	ledc_channel_t channel;
	static Dimmer *get(const char *name);
};

static Dimmer *Dimmers = 0;


Dimmer *Dimmer::get(const char *name)
{
	Dimmer *d = Dimmers;
	while (d && strcmp(d->name,name))
		d = d->next;
	return d;
}


static const char *dimmer_set_value(const char *name, unsigned v)
{
	Dimmer *d = Dimmer::get(name);
	if (d == 0)
		return "Unknown dimmer.";
	if (v > DIM_MAX)
		v = DIM_MAX;
	d->env->set(v);
	return 0;
}


int dimmer_set_perc(const char *name, float v)
{
	if (Dimmer *d = Dimmer::get(name)) {
		if ((v >= 0) && (v <= 100)) {
			d->env->set(v);
			return 0;
		}
	}
	return 1;
}


void dim_set(void *arg)
{
	const char *str = (const char *) arg;
	const char *s = strchr(str,':');
	if (s == 0)
		s = strchr(str,'=');
	if (s)
		++s;
	char *e;
	float f = strtof(s?s:str,&e);
	if ((e != s) && (f >= 0)) {
		float v = DIM_MAX;
		if ('%' == *e) {
			if (f < 100)
				v = (f*(float)DIM_MAX)/100.0;
		} else if (f < DIM_MAX) {
			v = f;
		}
		if (s) {
			char name[s-str];
			memcpy(name,str,s-str);
			name[s-str-1] = 0;
			log_dbug(TAG,"set %s %2.1f%%",name,v);
			if (Dimmer *d = Dimmer::get(name))
				d->env->set(v);
		} else {
			log_dbug(TAG,"set %2.1f%%",v);
			Dimmer *d = Dimmers;
			while (d) {
				d->env->set(v);
				d = d->next;
			}
		}
	} else {
		log_warn(TAG,"dim!set: invalid arg %s",str);
	}
}



#ifdef CONFIG_MQTT
static void mqtt_callback(const char *topic, const void *data, size_t len)
{
	const char *sl = strchr(topic,'/');
	if ((sl == 0) || (0 != memcmp(sl+1,"set_",4))) {
		log_warn(TAG,"unknown topic %s",topic);
		return;
	}
	log_info(TAG,"mqtt_cb: %s %.*s",sl+5,len,(const char *)data);
	const char *text = (const char *)data;
	char *ep = 0;
	float f = strtof(text,&ep);
	if (*ep == '%')
		dimmer_set_perc(sl+5,f);
	else
		dimmer_set_value(sl+5,f);
}
#endif


unsigned dimmer_fade(void *)
{
	unsigned ret = 100;
	unsigned s = Config.dim_step();
	Dimmer *d = Dimmers;
	while (d) {
		float set = d->env->get();
		if (set > DIM_MAX) {
			set = DIM_MAX;
			d->env->set(set);
		} else if (set < 0) {
			set = 0;
			d->env->set(0);
		}
		duty_t nv = (duty_t) set;
		if (nv == d->duty) {
			d = d->next;
			continue;
		}
		if (s == 0) {
			d->duty = nv;
		} else {
			if (s < ret)
				ret = s;
			if (nv > d->duty+DIM_STEP)
				d->duty += DIM_STEP;
			else if (nv < d->duty-DIM_STEP)
				d->duty -= DIM_STEP;
			else
				d->duty = nv;
		}
		log_dbug(TAG,"%s=%u %u",d->name,nv,d->duty);
		duty_t v = d->invert ? (DIM_MAX-d->duty) : d->duty;
#ifdef CONFIG_IDF_TARGET_ESP8266
		if (esp_err_t e = pwm_set_duty(d->channel,v))
			log_warn(TAG,"set duty %d",e);
		if (esp_err_t e = pwm_start())
			log_warn(TAG,"pwm start %d",e);
#elif defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
		ledc_set_duty(SPEED_MODE,d->channel,v);
		ledc_update_duty(SPEED_MODE,d->channel);
#else
#error missing implementation
#endif
		d = d->next;
	}
	return ret;
}


const char *dim(Terminal &t, int argc, const char *argv[])
{
	if (argc == 1) {
		Dimmer *d = Dimmers;
		while (d) {
			float cur = d->env->get();
			t.printf("%-12s %5u (%5.1f%%)\n",d->name,(unsigned)cur,(int)(cur*100.0)/(float)DIM_MAX);
			d = d->next;
		}
		return 0;
	}
	if (argc > 3)
		return "Invalid number of arguments.";
	const char *arg = argc == 2 ? argv[1] : argv[2];
	char *eptr;
	float f = strtof(arg,&eptr);
	if (eptr == arg) {
		if (strcmp(arg,"max"))
			return argc == 2 ? "Invalid argument #1." : "Invalid argument #2.";
		f = 100;
	}
	if (*eptr == 0) {
	} else if (*eptr == '%') {
		f = ((float)DIM_MAX * f) / 100;
	} else {
		if (0 == strcmp(arg,"max"))
			f = DIM_MAX;
		else
			return argc == 2 ? "Invalid argument #1." : "Invalid argument #2.";
	}
	t << f;
	t.println();
	if ((f < 0) || (f > DIM_MAX))
		return argc == 2 ? "Invalid argument #1." : "Invalid argument #2.";
	if (argc == 3)
		return dimmer_set_value(argv[1],f);
	Dimmer *d = Dimmers;
	while (d) {
		d->env->set(f);
		d = d->next;
	}
	return 0;
}


#ifdef CONFIG_AT_ACTIONS
static void dimmer_off(void *p)
{
	Dimmer *d = (Dimmer *)p;
	d->env->set(0);
}


static void dimmers_off(void *p)
{
	Dimmer *d = Dimmers;
	while (d) {
		d->env->set(0);
		d = d->next;
	}
}


static void dimmers_backup(void *p)
{
	Dimmer *d = Dimmers;
	while (d) {
		if (d->backup == 0)
			d->backup = d->env->get();
		log_dbug(TAG,"%s backup %f",d->name,d->backup);
		d = d->next;
	}
}


static void dimmer_backup(void *p)
{
	Dimmer *d = (Dimmer *)p;
	d->backup = d->env->get();
	nvm_store_float(d->name,d->backup);
	log_dbug(TAG,"%s backup %f",d->name,d->backup);
}


static void dimmers_restore(void *)
{
	Dimmer *d = Dimmers;
	while (d) {
		log_dbug(TAG,"%s restore %f",d->name,d->backup);
		d->env->set(d->backup);
		d = d->next;
	}
}


static void dimmer_on(void *p)
{
	Dimmer *d = (Dimmer *)p;
	d->env->set(DIM_MAX);
}


static void dimmer_restore(void *p)
{
	Dimmer *d = (Dimmer *)p;
	log_dbug(TAG,"%s restore %f",d->name,d->backup);
	d->env->set(d->backup);
}


static void dimmer_dec(void *p)
{
	Dimmer *d = (Dimmer *)p;
	d->env->set(d->env->get() - DIM_INC);
}


static void dimmer_inc(void *p)
{
	Dimmer *d = (Dimmer *)p;
	d->env->set(d->env->get() + DIM_INC);
}
#endif


#ifdef CONFIG_LUA
static int luax_dimmer_set(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	int v = luaL_checkinteger(L,2);
	if (const char *err = dimmer_set_value(n,v)) {
		lua_pushstring(L,err);
		lua_error(L);
	}
	return 0;
}


static LuaFn Functions[] = {
	{ "dimmer_set", luax_dimmer_set, "set dimmer value" },
	{ 0, 0, 0 }
};
#endif


int dimmer_setup()
{
	if (HWConf.led_size() == 0)
		return 0;
	unsigned nleds = 0;
	for (auto &conf : HWConf.led()) {
		if ((conf.pwm_ch() != -1) && (conf.gpio() != -1))
			++nleds;
	}
	if (nleds == 0)
		return 0;;
	log_info(TAG,"setup");
	unsigned freq = 1000;
	if (Config.has_pwm_freq()) {
		unsigned f = Config.pwm_freq();
		if ((f < 10) || (f > 50000)) {
			log_warn(TAG,"pwm frequency %u is out of range",f);
		} else {
			Period = 1000000 / f;
			freq = f;
			log_info(TAG,"frequency %u, period %u",freq,Period);
		}
	}
	unsigned err = 0;
#ifdef CONFIG_IDF_TARGET_ESP8266
	unsigned nch = 0;
	uint32_t pins[nleds];
	uint32_t duties[nleds];
#elif defined CONFIG_IDF_TARGET_ESP32
	ledc_timer_config_t tm;
	bzero(&tm,sizeof(tm));
	tm.duty_resolution = LEDC_TIMER_10_BIT;
	tm.freq_hz         = freq;
	tm.speed_mode      = SPEED_MODE;
	tm.timer_num       = LEDC_TIMER_0;
	tm.clk_cfg          = LEDC_AUTO_CLK;
	if (esp_err_t e = ledc_timer_config(&tm)) {
		log_error(TAG,"timer config %x",e);
		return e;
	}
#endif
	for (auto &conf : HWConf.led()) {
		if ((conf.pwm_ch() == -1) || (conf.gpio() == -1))
			continue;
		Dimmer *dim = new Dimmer;
		dim->next = Dimmers;
		Dimmers = dim;
		dim->gpio = (gpio_num_t)conf.gpio();
		dim->env = RTData->add(conf.name().c_str(),0.0,0,"%3.0f");
		dim->duty = 0;
		dim->invert = (conf.config() & 1) != 0;
		if (conf.has_name())
			dim->name = strdup(conf.name().c_str());
		else
			asprintf((char**)&dim->name,"dimmer@%u",dim->gpio);
		dim->backup = nvm_read_float(dim->name,0);
#ifdef CONFIG_IDF_TARGET_ESP8266
		pins[nch] = dim->gpio;
		dim->channel = nch;
		duties[nch] = (conf.config() & 1) ? DIM_MAX : 0;
		dim->duty = duties[nch];
		++nch;
#elif defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
		dim->channel = (ledc_channel_t) conf.pwm_ch();
		gpio_set_direction(dim->gpio,GPIO_MODE_OUTPUT);
		ledc_channel_config_t ch;
		bzero(&ch,sizeof(ch));
		ch.gpio_num   = dim->gpio;
		ch.channel    = dim->channel;
		ch.duty       = 0;
		ch.gpio_num   = dim->gpio;
		ch.speed_mode = SPEED_MODE;
		ch.timer_sel  = LEDC_TIMER_0;
		ch.hpoint     = 0;
		ch.intr_type  = LEDC_INTR_DISABLE;
		if (esp_err_t e = ledc_channel_config(&ch)) {
			log_error(TAG,"channel config %x",e);
			return e;
		}
#else
#error missing implementation
#endif
		log_dbug(TAG,"channel %u, gpio %u",dim->channel,dim->gpio);
#ifdef CONFIG_AT_ACTIONS
		action_add(concat(dim->name,"!on"),dimmer_on,dim,"turn on with PWM ramp");
		action_add(concat(dim->name,"!off"),dimmer_off,dim,"turn off with PWM ramp");
		action_add(concat(dim->name,"!dec"),dimmer_dec,dim,"decrement dimmer value");
		action_add(concat(dim->name,"!inc"),dimmer_inc,dim,"increment dimmer value");
		action_add(concat(dim->name,"!backup"),dimmer_backup,dim,"backup dimmer value");
		action_add(concat(dim->name,"!restore"),dimmer_restore,dim,"restore dimmer value");
#endif
#ifdef CONFIG_MQTT
		char topic[strlen(dim->name)+5] = "set_";
		strcpy(topic+4,dim->name);
		log_dbug(TAG,"subscribe %s",topic);
		mqtt_sub(topic,mqtt_callback);
#endif
	}
#ifdef CONFIG_AT_ACTIONS
	action_add("all_dimmers!backup",dimmers_backup,0,"backup dimmer and fade off");
	action_add("all_dimmers!off",dimmers_off,0,"backup dimmer and fade off");
	action_add("all_dimmers!restore",dimmers_restore,0,"restore dimmer from backup");
	action_add("dim!set",dim_set,0,"set dimmer(s) <d> to value <v>: arg = [<d>:]<v>");
#endif
#ifdef CONFIG_LUA
	xlua_add_funcs("dimmer",Functions);
#endif
#ifdef CONFIG_IDF_TARGET_ESP8266
	if (esp_err_t e = pwm_init(Period,duties,nch,pins)) {
		log_error(TAG,"pwm_init %x",e);
		return e;
	}
	for (int i = 0; i < nch; ++i)
		pwm_set_phase(i,0);
	if (esp_err_t e = pwm_start())
		log_warn(TAG,"pwm start %d",e);
#endif
	cyclic_add_task("dimmer",dimmer_fade);
	return err;
}

#endif
