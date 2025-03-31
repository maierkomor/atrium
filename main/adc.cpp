/*
 *  Copyright (C) 2020-2024, Thomas Maier-Komor
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

#include "adc.h"
#include "actions.h"
#include "cyclic.h"
#include "env.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "profiling.h"
#include "ringbuf.h"
#include "terminal.h"

#if IDF_VERSION > 50
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali_scheme.h>
#ifdef CONFIG_CORETEMP
#include <driver/temperature_sensor.h>
//#include <hal/adc_ll.h>
#include <hal/temperature_sensor_ll.h>
#include <soc/temperature_sensor_periph.h>
#endif
#else
#include <driver/adc.h>
#endif

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

#define TAG MODULE_ADC


#ifdef ESP32
#include <driver/rtc_io.h>
#if IDF_VERSION >= 50
#define DEFAULT_ADC_WIDTH ((adc_bitwidth_t)(SOC_ADC_DIGI_MAX_BITWIDTH))
#else
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
#endif

static adc_oneshot_unit_handle_t AdcHdl1 = 0, AdcHdl2 = 0;

struct AdcSignal : public EnvObject
{
#if IDF_VERSION >= 50
	AdcSignal(const char *name, adc_oneshot_unit_handle_t h, adc_unit_t u, adc_channel_t ch, adc_atten_t a, unsigned w)
#else
	AdcSignal(const char *name, adc_unit_t u, adc_channel_t ch, unsigned w)
#endif
	: EnvObject(name)
#if IDF_VERSION >= 50
	, hdl(h)
#endif
	, unit(u)
	, channel(ch)
	, atten(a)
	, ringbuf(w > 1 ? new SlidingWindow<uint16_t>(w) : 0)
	, raw("raw")
	, volt("voltage","mV")
	, phys("physical")
	{
		add(&raw);
		add(&volt);

#if IDF_VERSION >= 50
		cali = 0;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
		log_dbug(TAG, "calibration scheme is curve fitting");
		adc_cali_curve_fitting_config_t cali_config = {
			.unit_id = unit,
			.chan = ch,
			.atten = atten,
			.bitwidth = ADC_BITWIDTH_DEFAULT,
		};
		if (esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali))
			log_warn(TAG,"calibration failure: %s",esp_err_to_name(ret));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
		log_dbug(TAG, "calibration scheme is line fitting");
		adc_cali_line_fitting_config_t cali_config = {
			.unit_id = unit,
			.atten = atten,
			.bitwidth = ADC_BITWIDTH_DEFAULT,
#if CONFIG_IDF_TARGET_ESP32
			.default_vref = ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF,
#endif
		};
		if (esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_config, &cali))
			log_warn(TAG,"calibration failure: %s",esp_err_to_name(ret));
#endif
#endif
	}

	void set(float x)
	{
		float v = 0;
		raw.set(x);
#if IDF_VERSION >= 50
		if (cali) {
			int voltage;
			if (esp_err_t e = adc_cali_raw_to_voltage(cali, (int)x, &voltage))
				log_warn(TAG,"converting ADC%u,%u: %s",unit,channel,esp_err_to_name(e));
			else
				v = voltage;
		} else
#endif
		{
			switch (atten) {
			case ADC_ATTEN_DB_0:
	//			v = x * 950.0 / 4095.0;
				v = x * 750.0 / 4095.0;
				break;
			case ADC_ATTEN_DB_2_5:
	//			v = x * 1250.0 / 4095.0;
				v = x * 1050.0 / 4095.0;
				break;
			case ADC_ATTEN_DB_6:
	//			v = x * 1750.0 / 4095.0;
				v = x * 1300.0 / 4095.0;
				break;
			case ADC_ATTEN_DB_12:
	//			v = x * 2450.0 / 4095.0;
				v = x * 2500.0 / 4095.0;
				break;
			default:
				break;
			}
		}
		volt.set(v);
		phys.set(v*scale+offset);
	}

	void addPhysical(float s, float o, const char *dim)
	{
		scale = s;
		offset = o;
		phys.setDimension(dim);
		add(&phys);
	}

#if IDF_VERSION >= 50
	adc_oneshot_unit_handle_t hdl;
	adc_cali_handle_t cali;
#endif
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
#if IDF_VERSION >= 50
typedef adc_bitwidth_t adc_bits_width_t;
#else
static adc_bits_width_t U2Width = DEFAULT_ADC_WIDTH;
#endif


static void adc_sample_cb(void *arg)
{
	AdcSignal *s = (AdcSignal*)arg;
	int sample = 0;
	assert(s);
#if IDF_VERSION >= 50
	int raw;
	if (esp_err_t e = adc_oneshot_read(s->hdl, s->channel, &raw)) {
		log_warn(TAG,"reading ADC%u,%u: %s",s->unit,s->channel,esp_err_to_name(e));
		return;
	}
	sample = raw;
#else
	if (s->unit == 1) {
		sample = adc1_get_raw((adc1_channel_t)s->channel);
	} else if (s->unit == 2) {
		if (esp_err_t e = adc2_get_raw((adc2_channel_t)s->channel,U2Width,&sample)) {
			log_warn(TAG,"error reading adc2, channel %u, bits %u: %s",s->channel,U2Width,esp_err_to_name(e));
			return;
		}
	}
#endif
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


static const LuaFn Functions[] = {
	{ "adc_get_raw", luax_adc_get_raw, "get raw value of last ADC sample (name)" },
	{ "adc_get_volt", luax_adc_get_volt, "get voltage value from last  ADC sample (name)" },
	{ "adc_get_sample", luax_adc_sample, "get a sample from ADC (name)" },
	{ 0, 0, 0 }
};
#endif


//#if IDF_VERSION < 50 && defined CONFIG_IDF_TARGET_ESP32
#if defined CONFIG_IDF_TARGET_ESP32

static EnvNumber *Hall = 0;


static void hall_sample(void *)
{
	if (Hall) {
#if IDF_VERSION >= 50
		// TODO not implemented
		abort();
		int32_t v = 0;
#else
		int32_t v = hall_sensor_read();
#endif
		Hall->set(v);
	}
}


void hall_setup()
{
	if (!HWConf.has_adc() || HWConf.adc().hall_name().empty())
		return;
#if IDF_VERSION >= 50
//	adc_ll_hall_enable();
	// TODO implement
#else
	if (esp_err_t e = adc1_config_width(DEFAULT_ADC_WIDTH)) {
		log_error(TAG,"set hall sensor to 12bits: %s",esp_err_to_name(e));
		return;
	}
#endif
	Hall = new EnvNumber(HWConf.adc().hall_name().c_str());
	RTData->add(Hall);
	action_add("hall!sample",hall_sample,Hall,"take an hall-sensor sample");
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


static unsigned adc_cyclic_cb(void *arg)
{
	AdcSignal *s = (AdcSignal*)arg;
	adc_sample_cb(arg);
	return s->itv;
}


#ifdef CONFIG_CORETEMP

#if IDF_VERSION >= 50
static temperature_sensor_handle_t TSensHdl = 0;

// BEGIN import from IDF private header for BUG workaround
typedef enum { TEMP_SENSOR_FSM_INIT, TEMP_SENSOR_FSM_ENABLE, } temp_sensor_fsm_t;
struct temperature_sensor_obj_t {
	const temperature_sensor_attribute_t *tsens_attribute;
	temp_sensor_fsm_t  fsm;
	temperature_sensor_clk_src_t clk_src;
#if SOC_TEMPERATURE_SENSOR_INTR_SUPPORT
	intr_handle_t temp_sensor_isr_handle;
	temperature_thres_cb_t threshold_cbs;
	void *cb_user_arg;
#endif // SOC_TEMPERATURE_SENSOR_INTR_SUPPORT
};


static unsigned temp_cyclic(void *arg)
{
	PROFILE_FUNCTION();
	float celsius;
	if (0 == temperature_sensor_get_celsius(TSensHdl,&celsius)) {
		EnvNumber *t = (EnvNumber *) arg;
		t->set(celsius);
	}
	return 200;
}


void temp_sensor_setup()
{
	log_info(TAG,"temperature sensor setup");
	temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10,60);
	temperature_sensor_install(&cfg,&TSensHdl);
	EnvNumber *t = RTData->add("core-temperature",NAN,"\u00b0C","%4.1f");
	temperature_sensor_enable(TSensHdl);
	cyclic_add_task("coretemp",temp_cyclic,t,0);
}


#elif defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
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
#endif
#endif // CONFIG_CORETEMP


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
		if (Adcs[i]) {
			const char *name = Adcs[i]->name();
			float raw = Adcs[i]->raw.get();
			if (isnan(raw))
				t.printf("%-16s: <no sample>\n",name);
			else
				t.printf("%-16s: %5d (%gmV)\n",name,(int)raw,Adcs[i]->volt.get());
		} else
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


#if IDF_VERSION >= 50
adc_oneshot_unit_handle_t adc_get_handle(uint8_t unit)
{
	adc_unit_t u;
	if (unit == 1) {
		if (AdcHdl1 != 0)
			return AdcHdl1;
		u = ADC_UNIT_1;
	} else if (unit == 2) {
		if (AdcHdl2 != 0)
			return AdcHdl2;
		u = ADC_UNIT_2;
	} else {
		return 0;
	}
	return adc_get_unit_handle(u);
}


adc_oneshot_unit_handle_t adc_get_unit_handle(adc_unit_t unit)
{
	adc_oneshot_unit_handle_t *hdlptr;
	adc_oneshot_unit_init_cfg_t ucfg;
	if (unit == ADC_UNIT_1) {
		if (AdcHdl1 != 0)
			return AdcHdl1;
		ucfg.unit_id = ADC_UNIT_1;
		hdlptr = &AdcHdl1;
	} else if (unit == ADC_UNIT_2) {
		if (AdcHdl2 != 0)
			return AdcHdl2;
		ucfg.unit_id = ADC_UNIT_2;
		hdlptr = &AdcHdl2;
	} else {
		return 0;
	}
//	ucfg.clk_src = ADC_DEFAULT_CLOCK;
	ucfg.clk_src = (adc_oneshot_clk_src_t)0;
	ucfg.ulp_mode = ADC_ULP_MODE_DISABLE;
	if (esp_err_t e = adc_oneshot_new_unit(&ucfg,hdlptr)) {
		log_warn(TAG,"ADC unit %d: %s",unit+1,esp_err_to_name(e));
	}
	return *hdlptr;
}
#endif


void adc_setup()
{
#ifdef CONFIG_LUA
	xlua_add_funcs("adc",Functions);
#endif
#ifdef CONFIG_CORETEMP
	temp_sensor_setup();
#endif
	assert(Adcs == 0);
	if (!HWConf.has_adc())
		return;
	const auto &conf = HWConf.adc();
	adc_bitwidth_t w1 = ADC_BITWIDTH_DEFAULT;
	adc_bitwidth_t w2 = ADC_BITWIDTH_DEFAULT;
#if IDF_VERSION >= 50
	if (conf.has_adc1_bits())
		w1 = (adc_bitwidth_t) (ADC_BITWIDTH_9 + conf.adc1_bits());
	if (conf.has_adc2_bits())
		w2 = (adc_bitwidth_t) (ADC_BITWIDTH_9 + conf.adc2_bits());
#else
	if (conf.adc1_bits()) {
		if (esp_err_t e = adc_set_data_width(ADC_UNIT_1,(adc_bits_width_t)conf.adc1_bits()))
			log_warn(TAG,"set adc1 data width %dbits: %s",conf.adc1_bits()+9,esp_err_to_name(e));
	}
#ifndef CONFIG_IDF_TARGET_ESP32C3
	if (conf.has_adc2_bits()) {
		if (esp_err_t e = adc_set_data_width(ADC_UNIT_2,(adc_bits_width_t)conf.adc2_bits()))
			log_warn(TAG,"set adc2 data width %dbits: %s",conf.adc2_bits()+9,esp_err_to_name(e));
		else
			U2Width = (adc_bits_width_t)conf.adc2_bits();
	} else {
		U2Width = DEFAULT_ADC_WIDTH;
	}
#endif
#endif
	unsigned num = 0;
	NumAdc = conf.channels_size();
	log_info(TAG,"%u channels",NumAdc);
	Adcs = (AdcSignal **) malloc(sizeof(AdcSignal*)*NumAdc);
	bzero(Adcs,sizeof(AdcSignal*)*NumAdc);
	for (const AdcChannel &c : conf.channels()) {
		unsigned u = c.unit();
		adc_unit_t unit;
		switch (u) {
		case 1:
			unit = ADC_UNIT_1;
			break;
		case 2:
			unit = ADC_UNIT_2;
			break;
		default:
			log_warn(TAG,"invalid unit %u",u);
			continue;
		}
		int ch = c.ch();
#if IDF_VERSION >= 50
		if (ch >= SOC_ADC_CHANNEL_NUM(unit)) {
#else
		if (((unit == ADC_UNIT_1) && (ch >= ADC1_CHANNEL_MAX)) || ((unit == ADC_UNIT_2) && (ch >= ADC2_CHANNEL_MAX))) {
#endif
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
		int gpio = -1;
		adc_oneshot_channel_to_io(unit,(adc_channel_t)ch,&gpio);
		log_info(TAG,"init %s on ADC%u.%u at GPIO%u, atten %u",n,u,ch,gpio,atten);
#if IDF_VERSION >= 50
		adc_oneshot_unit_handle_t hdl = adc_get_unit_handle(unit);
		if (hdl == 0) {
			log_warn(TAG,"failed to get ADC unit handle for unit %d",u);
			continue;
		}
		adc_oneshot_chan_cfg_t cc;
		cc.atten = atten;
		cc.bitwidth = unit == ADC_UNIT_1 ? w1 : w2;
		adc_oneshot_config_channel(hdl,(adc_channel_t)ch,&cc);
		Adcs[num] = new AdcSignal(n,hdl,unit,(adc_channel_t)ch,atten,c.window());
#else
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
		Adcs[num] = new AdcSignal(n,unit,(adc_channel_t)ch,c.window());
#endif
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
	}
	NumAdc = num;
#if IDF_VERSION < 50
	if (num)
		adc_power_acquire();
#ifndef CONFIG_IDF_TARGET_ESP32C3
	if (esp_err_t e = adc_set_data_inv(ADC_UNIT_1,true))
		log_warn(TAG,"set non-inverting mode on ADC1: %s",esp_err_to_name(e));
#endif
#endif
}


const char *adc(Terminal &term, int argc, const char *args[])
{
	if (argc == 1)
		return adc_print(term,0);;
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
		adc_setup();
		return 0;
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
