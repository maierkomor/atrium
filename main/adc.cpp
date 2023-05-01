/*
 *  Copyright (C) 2020-2023, Thomas Maier-Komor
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

#include "actions.h"
#include "cyclic.h"
#include "env.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "ringbuf.h"
#include "terminal.h"

#include <driver/adc.h>

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

#define TAG MODULE_ADC


#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
#include <driver/rtc_io.h>
#define DEFAULT_ADC_WIDTH ((adc_bits_width_t)((int)ADC_WIDTH_MAX-1))
#if defined CONFIG_IDF_TARGET_ESP32
#include <soc/sens_reg.h>
#elif defined CONFIG_IDF_TARGET_ESP32S2
#include <soc/sens_reg.h>
#include <driver/temp_sensor.h>
#elif defined CONFIG_IDF_TARGET_ESP32S3
#include <soc/sens_reg.h>
#include <driver/temp_sensor.h>
#elif defined CONFIG_IDF_TARGET_ESP32C3
#include <driver/temp_sensor.h>
#endif


struct AdcSignal : public EnvObject
{
	AdcSignal(const char *name, adc_unit_t u, adc_channel_t ch)
	: EnvObject(name)
	, unit(u)
	, channel(ch)
	, raw("raw")
	, volt("voltage","mV")
	, phys("physical","","%4.0f")
	{
		add(&raw);
		add(&volt);
		add(&phys);
	}

	AdcSignal(const char *name, adc_unit_t u, adc_channel_t ch, unsigned w)
	: EnvObject(name)
	, unit(u)
	, channel(ch)
	, ringbuf(new SlidingWindow<uint16_t>(w))
	, raw("raw")
	, volt("voltage","mV")
	, phys("physical")
	{
		add(&raw);
		add(&volt);
	}

	void set(float x)
	{
		raw.set(x);
		float v = 0;
		switch (atten) {
		case ADC_ATTEN_DB_0:
			v = x * 950.0 / 4095.0;
			break;
		case ADC_ATTEN_DB_2_5:
			v = x * 1250.0 / 4095.0;
			break;
		case ADC_ATTEN_DB_6:
			v = x * 1750.0 / 4095.0;
			break;
		case ADC_ATTEN_DB_11:
			v = x * 2450.0 / 4095.0;
			break;
		default:
			break;
		}
		volt.set(v);
		phys.set(x*scale+offset);
	}

	void addPhysical(float s, float o, const char *dim)
	{
		scale = s;
		offset = o;
		phys.setDimension(dim);
		add(&phys);
	}

	adc_unit_t unit;
	adc_channel_t channel;
	adc_atten_t atten;
	unsigned itv = 0;
	SlidingWindow<uint16_t> *ringbuf = 0;
	EnvNumber raw, volt, phys;
	float scale = 1, offset = 0;
};


static unsigned NumAdc = 0;
static AdcSignal **Adcs = 0;
static adc_bits_width_t U2Width = DEFAULT_ADC_WIDTH;

#if defined CONFIG_IDF_TARGET_ESP32
static EnvNumber *Hall = 0;


static void hall_sample(void *)
{
	if (Hall) {
		int32_t v = hall_sensor_read();
		Hall->set(v);
	}
}


int hall_setup()
{
	if (!HWConf.has_adc() || HWConf.adc().hall_name().empty())
		return 0;
	if (esp_err_t e = adc1_config_width(DEFAULT_ADC_WIDTH)) {
		log_error(TAG,"set hall sensor to 12bits: %s",esp_err_to_name(e));
		return 1;
	} else {
		Hall = new EnvNumber(HWConf.adc().hall_name().c_str());
		RTData->add(Hall);
		action_add("hall!sample",hall_sample,Hall,"take an hall-sensor sample");
	}
	return 0;
}


const char *hall(Terminal &term, int argc, const char *args[])
{
	if (Hall == 0) {
		return "Not initialized.";
	}
	if (argc == 1) {
		term.printf("%g\n",Hall->get());
		return 0;
	}
	if (!strcmp(args[1],"sample")) {
		hall_sample(0);
		return 0;
	}
	return "Invalid argument #1.";
}
#endif // CONFIG_IDF_TARGET_ESP32


static void adc_sample_cb(void *arg)
{
	AdcSignal *s = (AdcSignal*)arg;
	int sample = 0;
	assert(s);
	if (s->unit == 1) {
		sample = adc1_get_raw((adc1_channel_t)s->channel);
	} else if (s->unit == 2) {
		if (esp_err_t e = adc2_get_raw((adc2_channel_t)s->channel,U2Width,&sample)) {
			log_warn(TAG,"error reading adc2, channel %u, bits %u: %s",s->channel,U2Width,esp_err_to_name(e));
			return;
		}
	}
	if (s->ringbuf) {
		s->ringbuf->put(sample);
		float a = s->ringbuf->avg();
		log_dbug(TAG,"sample %s: %u, average %f, sum %u",s->name(),sample,a,s->ringbuf->sum());
		s->set(a);
	} else {
		log_dbug(TAG,"sample %s: %u",s->name(),sample);
		s->set(sample);
	}
}


static unsigned adc_cyclic_cb(void *arg)
{
	AdcSignal *s = (AdcSignal*)arg;
	adc_sample_cb(arg);
	return s->itv;
}


#if defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
static unsigned temp_cyclic(void *arg)
{
	float celsius;
	if (0 == temp_sensor_read_celsius(&celsius)) {
		EnvNumber *t = (EnvNumber *) arg;
		t->set(celsius);
	}
	return 50;
}


void temp_sensor_setup()
{
	log_info(TAG,"temperature sensor setup");
	temp_sensor_config_t cfg = TSENS_CONFIG_DEFAULT();
	temp_sensor_set_config(cfg);
	if (0 == temp_sensor_start()) {
		EnvNumber *t = RTData->add("core-temperature",NAN,"\u00b0C","%4.1f");
		cyclic_add_task("coretemp",temp_cyclic,t,0);
	} else {
		log_warn(TAG,"temperature sensor init failed");
	}
}
#else
#define temp_sensor_setup()
#endif


static AdcSignal *getAdc(const char *arg)
{
	char *e;
	long l = strtol(arg,&e,0);
	if (e == arg) {
		unsigned x = 0;
		AdcSignal *s = Adcs[x];
		while (s && strcmp(s->name(),arg)) {
			++x;
			if (x == NumAdc)
				return 0;
			s = Adcs[x];
		}
		return s;
	} else if ((l < 0) || (l >= NumAdc)) {
		return 0;
	}
	return Adcs[l];
}


static const char *adc_print(Terminal &t, const char *arg)
{
	if (Adcs == 0)
		return "No ADCs configured.";
	if (arg) {
		AdcSignal *s = getAdc(arg);
		if (s == 0) {
			t.printf("unknown ADC %s",arg);
			return "";
		}
		t.printf("%-16s: %5d (%gmV)\n",s->name(),(int)s->raw.get(),s->volt.get());
	} else for (unsigned i = 0; i < NumAdc; ++i) {
		if (Adcs[i])
			t.printf("%-16s: %5d (%gmV)\n",Adcs[i]->name(),(int)Adcs[i]->raw.get(),Adcs[i]->volt.get());
		else
			t.printf("%u not initialized\n",i);
	}
	return 0;
}


static const char *adc_sample(Terminal &t, const char *arg)
{
	if (arg == 0) {
		for (int i = 0; i < NumAdc; ++i)
			adc_sample_cb(Adcs[i]);
	} else if (AdcSignal *s = getAdc(arg)) {
		adc_sample_cb(s);
	} else {
		return "Invalid argument.";
	}
	return adc_print(t,arg);
}


int adc_setup()
{
	temp_sensor_setup();
	assert(Adcs == 0);
	if (!HWConf.has_adc())
		return 0;
	const auto &conf = HWConf.adc();
#ifndef CONFIG_IDF_TARGET_ESP32C3
	if (conf.adc1_bits()) {
		if (esp_err_t e = adc_set_data_width(ADC_UNIT_1,(adc_bits_width_t)conf.adc1_bits()))
			log_warn(TAG,"set adc1 data width %dbits: %s",conf.adc1_bits()+9,esp_err_to_name(e));
	}
	if (conf.has_adc2_bits()) {
		if (esp_err_t e = adc_set_data_width(ADC_UNIT_2,(adc_bits_width_t)conf.adc2_bits()))
			log_warn(TAG,"set adc2 data width %dbits: %s",conf.adc2_bits()+9,esp_err_to_name(e));
		else
			U2Width = (adc_bits_width_t)conf.adc2_bits();
	} else {
		U2Width = DEFAULT_ADC_WIDTH;
	}
#endif
	unsigned num = 0;
	NumAdc = conf.channels_size();
	log_info(TAG,"%u channels",NumAdc);
	Adcs = (AdcSignal **) malloc(sizeof(AdcSignal*)*NumAdc);
	bzero(Adcs,sizeof(AdcSignal*)*NumAdc);
	for (const AdcChannel &c : conf.channels()) {
		unsigned u = c.unit();
		if ((u < 1) || (u > 2)) {
			log_warn(TAG,"invalid unit %u",u);
			continue;
		}
		int ch = c.ch();
		if (((u == 1) && (ch >= ADC1_CHANNEL_MAX)) || ((u == 2) && (ch >= ADC2_CHANNEL_MAX))) {
			log_warn(TAG,"invalid channel %u",ch);
			continue;
		}
		const char *n = c.name().c_str();
		if (n[0] == 0) {
			char name[16];
			sprintf(name,"adc%u.%u",u,ch);
			n = strdup(name);
			log_dbug(TAG,"created name %s",name);
		}
		adc_atten_t atten = (adc_atten_t)c.atten();
		log_dbug(TAG,"init %s on ADC%u.%u, atten %u",n,u,ch,atten);
		if (u == 1) {
			if (esp_err_t e = adc1_config_channel_atten((adc1_channel_t)ch,atten)) {
				log_warn(TAG,"set ADC1.%u atten: %s",ch,esp_err_to_name(e));
				continue;
			}
		} else if (u == 2) {
			// ADC2 is used by WiFi on ESP32
			// ADC2 is defect ESP32-C3 at least <= 0.4
			// i.e. it is normally not usable
			// consult the IDF docu for details
			if (esp_err_t e = adc2_config_channel_atten((adc2_channel_t)ch,atten)) {
				log_warn(TAG,"set ADC2.%u atten: %s",ch,esp_err_to_name(e));
				continue;
			}
		}
		if (auto w = c.window()) {
			Adcs[num] = new AdcSignal(n,(adc_unit_t)u,(adc_channel_t)ch,w);
		} else {
			Adcs[num] = new AdcSignal(n,(adc_unit_t)u,(adc_channel_t)ch);
		}
		Adcs[num]->atten = atten;
		if (c.has_scale() || c.has_offset() || c.has_dim()) {
			Adcs[num]->addPhysical(c.scale(),c.offset(),c.dim().c_str());
		}
		RTData->add(Adcs[num]);
		if (auto i = c.interval()) {
			Adcs[num]->itv = i;
			cyclic_add_task(concat("adc_sample_",n), adc_cyclic_cb, Adcs[num], 0);
		} else {
			action_add(concat(n,"!sample"),adc_sample_cb,Adcs[num],"take an ADC sample");
		}
		++num;
		log_info(TAG,"%s on ADC%u.%u",n,u,ch);
	}
	NumAdc = num;
	if (num)
		adc_power_acquire();
#ifndef CONFIG_IDF_TARGET_ESP32C3
	if (esp_err_t e = adc_set_data_inv(ADC_UNIT_1,true))
		log_warn(TAG,"set non-inverting mode on ADC1: %s",esp_err_to_name(e));
#endif
	return 0;
}


const char *adc(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("adc is %sinitialized\n",Adcs ? "" : "not ");
		return 0;
	}
#if 0 // for debugging purposes
	term.printf("read ctrl 0x%lx\n",REG_READ(SENS_SAR_READ_CTRL_REG));
	term.printf("sar start 0x%lx\n",REG_READ(SENS_SAR_START_FORCE_REG));
	term.printf("meas st1  0x%lx\n",REG_READ(SENS_SAR_MEAS_START1_REG));
	term.printf("read ctrl2 0x%lx\n",REG_READ(SENS_SAR_READ_CTRL2_REG));
	term.printf("meas st2  0x%lx\n",REG_READ(SENS_SAR_MEAS_START2_REG));
#endif
	if (!strcmp(args[1],"init")) {
		if (term.getPrivLevel() == 0) {
			return "Access denied.";
		}
		return adc_setup() ? "Failed." : 0;
	}
	if (!strcmp(args[1],"print"))
		return adc_print(term,(argc > 2) ? args[2] : 0);
	if (!strcmp(args[1],"sample"))
		return adc_sample(term,(argc > 2) ? args[2] : 0);
	return "Invalid argument #1.";
}


#elif defined CONFIG_IDF_TARGET_ESP8266
static EnvNumber *Adc = 0;


static void adc_sample_cb(void *arg = 0)
{
	uint16_t adc;
	adc_read(&adc);
	Adc->set(adc);
}


static const char *adc_print(Terminal &term)
{
	term.printf("%u\n",(unsigned)Adc->get());
	return 0;
}


static const char *adc_sample(Terminal &term)
{
	adc_sample_cb();
	return adc_print(term);
}


#ifdef CONFIG_LUA
static int luax_adc_get_raw(lua_State *L)
{
	// adc_get(name)
	const char *name = luaL_checkstring(L,1);
	AdcSignal *adc = getAdc(name);
	if (adc == 0) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	lua_pushnumber(L,adc->raw.get());
	return 1;
}


static int luax_adc_get_volt(lua_State *L)
{
	// adc_get(name)
	const char *name = luaL_checkstring(L,1);
	AdcSignal *adc = getAdc(name);
	if (adc == 0) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	lua_pushnumber(L,adc->volt.get());
	return 1;
}


static int luax_adc_sample(lua_State *L)
{
	// adc_get(name)
	const char *name = luaL_checkstring(L,1);
	AdcSignal *adc = getAdc(name);
	if (adc == 0) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	adc_sample_cb(adc);
	return 0;
}


static LuaFn Functions[] = {
	{ "adc_get_raw", luax_adc_get_raw, "get raw value of last ADC sample (name)" },
	{ "adc_get_volt", luax_adc_get_volt, "get voltage value from last  ADC sample (name)" },
	{ "adc_get_sample", luax_adc_sample, "get a sample from ADC (name)" },
	{ 0, 0, 0 }
};
#endif


int adc_setup()
{
	if (!HWConf.has_adc())
		return 0;
	if (Adc) {
		log_warn(TAG,"already initialized");
		return 1;
	}
	const AdcConfig &c = HWConf.adc();
	if (!c.has_adc_name()) {
		log_warn(TAG,"need ADC name");
		return 1;
	}
	adc_config_t cfg;
	if (c.has_mode())
		cfg.mode = (adc_mode_t) c.mode();
	else
		cfg.mode = ADC_READ_TOUT_MODE;
	if (c.has_clk_div())
		cfg.clk_div = c.clk_div();
	else
		cfg.clk_div = 32;
	if (esp_err_t e = adc_init(&cfg)) {
		log_warn(TAG,"ADC init failed: %s",esp_err_to_name(e));
		return 1;
	}
	const char *n = HWConf.adc().adc_name().c_str();
	Adc = new EnvNumber(n);
	action_add(concat(n,"!sample"),adc_sample_cb,0,"take an ADC sample");
#ifdef CONFIG_LUA
	xlua_add_funcs("adc",Functions);
#endif
	return 0;
}


const char *adc(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("ADC is %sinitialized\n",Adc ? "" : "not ");
		return 0;
	}
	if (!strcmp(args[1],"init")) {
		if (term.getPrivLevel() == 0) {
			return "Access denied.";
		}
		return adc_setup() ? "Failed." : 0;
	}
	if (Adc == 0)
		return "Not initialized.";
	if (!strcmp(args[1],"print"))
		return adc_print(term);
	if (!strcmp(args[1],"sample"))
		return adc_sample(term);
	return "Invalid argument.";
}

#else	// other IDF
#error unknown target
#endif
