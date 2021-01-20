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

#ifdef CONFIG_DIMMER

#include <errno.h>
#include <stddef.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <driver/pwm.h>
#elif defined CONFIG_IDF_TARGET_ESP32
#include <driver/ledc.h>
#else
#error missing implementation
#endif
#include <driver/gpio.h>

#include "actions.h"
#include "binformats.h"
#include "cyclic.h"
#include "event.h"
#include "globals.h"
#include "dimmer.h"
#include "log.h"
#include "support.h"
#include "terminal.h"
#include "ujson.h"

#define PWM_BITS 8

#define DIM_MAX ((1<<PWM_BITS)-1)

#if PWM_BITS <= 8
typedef uint8_t duty_t;
#elif PWM_BITS <= 16
typedef uint16_t duty_t;
#elif PWM_BITS <= 32
typedef uint32_t duty_t;
#endif


static char TAG[] = "dim";

struct Dimmer
{
	const char *name;
	gpio_num_t gpio;
	unsigned channel;
	duty_t duty_set,duty_cur;
	JsonInt *json;
	Dimmer *next;
};

static Dimmer *Dimmers = 0;


static Dimmer *get_dimmer(const char *name)
{
	Dimmer *d = Dimmers;
	while (d && strcmp(d->name,name))
		d = d->next;
	return d;
}


int dimmer_set_value(const char *name, unsigned v)
{
	Dimmer *d = get_dimmer(name);
	if (d == 0)
		return 1;
	if (v > DIM_MAX)
		return EINVAL;
	d->duty_set = v;
	d->json->set(v);
	return 0;
}


unsigned dimmer_get_value(const char *n)
{
	Dimmer *d = get_dimmer(n);
	if (d == 0)
		return UINT32_MAX;
	return d->duty_set;
}


unsigned dimmer_fade(void *)
{
	unsigned ret = 500;
	Dimmer *d = Dimmers;
	while (d) {
		auto cur = d->duty_cur;
		if (cur == d->duty_set) {
			d = d->next;
			continue;
		}
		ret = Config.dim_step();
		if (d->duty_set > cur)
			++cur;
		else
			--cur;
		d->duty_cur = cur;
		log_info(TAG,"%u",cur);
#ifdef CONFIG_IDF_TARGET_ESP8266
		pwm_set_duty(d->channel,cur);
		pwm_start();
#elif defined CONFIG_IDF_TARGET_ESP32
		ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE,(ledc_channel_t)d->channel,cur,DIM_MAX);
#else
#error missing implementation
#endif
		d = d->next;
	}
	return ret;
}


int dim(Terminal &t, int argc, const char *argv[])
{
	if (argc == 1) {
		Dimmer *d = Dimmers;
		while (d) {
			t.printf("%s %u\n",d->name,d->duty_cur);
			d = d->next;
		}
		return 0;
	}
	Dimmer *d = get_dimmer(argv[1]);
	if (d == 0) {
		t.printf("unknown led name\n");
		return 1;
	}
	if (argc == 2) {
		t.printf("%u\n",d->duty_cur);
		return 0;
	}
	long l = strtol(argv[2],0,0);
	if ((l < 0) || (l > DIM_MAX)) {
		t.printf("invalid argument - valid range: 0-%u\n",DIM_MAX);
		return 1;
	}
	return dimmer_set_value(argv[1],l);
}


#ifdef CONFIG_AT_ACTIONS
static void dimmer_off(void *p)
{
	Dimmer *d = (Dimmer *)p;
	d->duty_set = 0;
}


static void dimmer_on(void *p)
{
	Dimmer *d = (Dimmer *)p;
	d->duty_set = DIM_MAX;
}
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
	unsigned err = 0;
	log_info(TAG,"setup");
#ifdef CONFIG_IDF_TARGET_ESP8266
	unsigned nch = 0;
	uint32_t pins[nleds];
	uint32_t duties[nleds];
#elif defined CONFIG_IDF_TARGET_ESP32
	ledc_timer_config_t tm;
	tm.duty_resolution = LEDC_TIMER_8_BIT;
	tm.freq_hz         = 1000;
	tm.speed_mode      = LEDC_LOW_SPEED_MODE;
	tm.timer_num       = LEDC_TIMER_0;
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
		dim->channel = conf.pwm_ch();
		dim->json = RTData->add(conf.name().c_str(),0);
		if (conf.has_name())
			dim->name = strdup(conf.name().c_str());
		else
			asprintf((char**)&dim->name,"dimmer@%u",dim->gpio);
#ifdef CONFIG_IDF_TARGET_ESP8266
		pins[nch] = dim->gpio;
		duties[nch] = (conf.config() & 1) ? DIM_MAX : 0;
		++nch;
#elif defined CONFIG_IDF_TARGET_ESP32
		ledc_channel_config_t ch;
		ch.channel    = (ledc_channel_t)conf.pwm_ch();
		ch.duty       = 0;
		ch.gpio_num   = conf.gpio();
		ch.speed_mode = LEDC_LOW_SPEED_MODE;
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
		if (esp_err_t e = ledc_set_pin((gpio_num_t)conf.gpio(),LEDC_LOW_SPEED_MODE,(ledc_channel_t)conf.pwm_ch())) {
			log_error(TAG,"set pwm on gpio %u failed: %s",conf.gpio(),esp_err_to_name(e));
			++err;
		}
#else
#error missing implementation
#endif
#ifdef CONFIG_AT_ACTIONS
		action_add(concat(dim->name,"!on"),dimmer_on,dim,"turn on with PWM ramp");
		action_add(concat(dim->name,"!off"),dimmer_off,dim,"turn off with PWM ramp");
#endif
	}
#ifdef CONFIG_IDF_TARGET_ESP8266
	if (esp_err_t e = pwm_init(DIM_MAX,duties,1,pins)) {
		log_error(TAG,"pwm_init %x",e);
		return e;
	}
	pwm_start();
#endif
	cyclic_add_task("dimmer",dimmer_fade);
	return err;
}

#endif
