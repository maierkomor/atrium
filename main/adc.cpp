/*
 *  Copyright (C) 2020-2022, Thomas Maier-Komor
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
#include "dataflow.h"
#include "env.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "terminal.h"

#include <driver/adc.h>

#define TAG MODULE_ADC

#if defined CONFIG_IDF_TARGET_ESP32
#include <driver/rtc_io.h>
#include <soc/sens_reg.h>

struct AdcSignal : public EnvNumber
{
	AdcSignal(const char *name, adc_unit_t u, adc_channel_t ch)
	: EnvNumber(name)
	, unit(u)
	, channel(ch)
	{

	}

	adc_unit_t unit;
	adc_channel_t channel;
};

static unsigned NumAdc = 0;
static AdcSignal **Adcs = 0;
static EnvNumber *Hall = 0;
static adc_bits_width_t U2Width;

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
	if (esp_err_t e = adc1_config_width(ADC_WIDTH_BIT_12)) {
		log_error(TAG,"set hall sensor to 12bits: %s",esp_err_to_name(e));
		return 1;
	} else {
		Hall = new EnvNumber(HWConf.adc().hall_name().c_str());
		RTData->add(Hall);
		action_add("hall!sample",hall_sample,Hall,"take an hall-sensor sample");
	}
	return 0;
}


int hall(Terminal &term, int argc, const char *args[])
{
	if (Hall == 0) {
		term.printf("not initialized\n");
		return 1;
	}
	if (argc == 1) {
		term.printf("%g\n",Hall->get());
		return 0;
	}
	if (!strcmp(args[1],"sample")) {
		hall_sample(0);
		return 0;
	}
	return 1;
}


static void adc_sample_cb(void *arg)
{
	AdcSignal *s = (AdcSignal*)arg;
	int sample = 0;
	if (s == 0)
		return;
	else if (s->unit == 1)
		sample = adc1_get_raw((adc1_channel_t)s->channel);
	else if (s->unit == 2) {
		if (esp_err_t e = adc2_get_raw((adc2_channel_t)s->channel,U2Width,&sample)) {
			log_warn(TAG,"error reading adc2, channel %u: %s",s->channel,esp_err_to_name(e));
			return;
		}
	}
	log_dbug(TAG,"sample %s: %u",s->name(),sample);
	s->set(sample);
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


static int adc_print(Terminal &t, const char *arg)
{
	if (Adcs == 0)
		return 1;
	if (arg) {
		AdcSignal *s = getAdc(arg);
		if (s == 0) {
			t.printf("unknown adc %s",arg);
			return 1;
		}
		t.printf("%-16s: %5d\n",s->name(),(int)s->get());
	} else for (unsigned i = 0; i < NumAdc; ++i) {
		if (Adcs[i])
			t.printf("%-16s: %5d\n",Adcs[i]->name(),(int)Adcs[i]->get());
		else
			t.printf("%u not initialized\n",i);
	}
	return 0;
}


static int adc_sample(Terminal &t, const char *arg)
{
	if (arg == 0) {
		for (int i = 0; i < NumAdc; ++i)
			adc_sample_cb(Adcs[i]);
	} else if (AdcSignal *s = getAdc(arg)) {
		adc_sample_cb(s);
	} else {
		t.printf("invalid argument");
		return 1;
	}
	return adc_print(t,arg);
}


int adc_setup()
{
	assert(Adcs == 0);
	if (!HWConf.has_adc())
		return 0;
	const auto &conf = HWConf.adc();
	if (conf.adc1_bits()) {
		if (esp_err_t e = adc_set_data_width(ADC_UNIT_1,(adc_bits_width_t)conf.adc1_bits()))
			log_warn(TAG,"set adc1 data width %dbits: %s",conf.adc1_bits()+9,esp_err_to_name(e));
	}
	if (conf.has_adc2_bits()) {
		if (esp_err_t e = adc_set_data_width(ADC_UNIT_2,(adc_bits_width_t)conf.adc2_bits()))
			log_warn(TAG,"set adc2 data width %dbits: %s",conf.adc2_bits()+9,esp_err_to_name(e));
		else
			U2Width = (adc_bits_width_t)conf.adc2_bits();
	} else
		U2Width = ADC_WIDTH_BIT_12;
	NumAdc = conf.channels_size();
	log_info(TAG,"%u channels",NumAdc);
	Adcs = (AdcSignal **) malloc(sizeof(AdcSignal*)*NumAdc);
	bzero(Adcs,sizeof(AdcSignal*)*NumAdc);
	for (unsigned x = 0; x < NumAdc; ++x) {
		const AdcChannel &c = conf.channels(x);
		if (!c.has_ch() || !c.has_unit())
			continue;
		char name[8];
		unsigned u = c.unit();
		if ((u == 0) || (u > 2)) {
			log_error(TAG,"invalid unit %u",u);
			continue;
		}
		unsigned ch = c.ch();
		if (((u == 1) && (ch >= ADC1_CHANNEL_MAX)) || ((u == 2) && (ch >= ADC2_CHANNEL_MAX))) {
			log_error(TAG,"invalid channel %u",ch);
			continue;
		}
		const char *n = c.name().c_str();
		if (n[0] == 0) {
			sprintf(name,"adc%u.%u",u,ch);
			n = strdup(name);
		}
		if (u == 2) {
			// ADC2 is used by WiFi 
			// i.e. it is normally not usable
			// consult the IDF docu for details
			if (adc2_config_channel_atten((adc2_channel_t)ch,ADC_ATTEN_DB_0))
				log_warn(TAG,"unit 2 channel %u init failed",ch);
			/*
			gpio_num_t gpio = (gpio_num_t)(10+ch);
			//REG_WRITE(0x10+(gpio * 4), 0x300);
			//REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + (num * 4), SIG_GPIO_OUT_IDX);
			if (rtc_gpio_init(gpio))
				log_warn(TAG,"init gpio %u of ADC",gpio);
			else if (rtc_gpio_isolate(gpio))
				log_warn(TAG,"gpio %u isolate failed",gpio);
			*/
		} else if (esp_err_t e = adc_gpio_init(ADC_UNIT_1,(adc_channel_t)ch)) {
			log_error(TAG,"unable to init channel %u of adc%u: %s",ch,u,esp_err_to_name(e));
			continue;
		} else if (u == 1) {
			if (esp_err_t e = adc1_config_channel_atten((adc1_channel_t)ch,(adc_atten_t)c.atten())) {
				log_warn(TAG,"set adc[%u] atten: %s",ch,esp_err_to_name(e));
				continue;
			}
		} else if (u == 2) {
			if (esp_err_t e = adc2_config_channel_atten((adc2_channel_t)ch,(adc_atten_t)c.atten())) {
				log_warn(TAG,"set adc[%u] atten: %s",ch,esp_err_to_name(e));
				continue;
			}
		}
		Adcs[x] = new AdcSignal(n,(adc_unit_t)u,(adc_channel_t)ch);
		RTData->add(Adcs[x]);
		action_add(concat(n,"!sample"),adc_sample_cb,Adcs[x],"take an ADC sample");
		log_info(TAG,"initialized ADC%u.%u",u,ch);
	}

	return 0;
}


int adc(Terminal &term, int argc, const char *args[])
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
			term.printf("Access denied.\n");
			return 1;
		}
		return adc_setup();
	}
	if (!strcmp(args[1],"print"))
		return adc_print(term,(argc > 2) ? args[2] : 0);
	if (!strcmp(args[1],"sample"))
		return adc_sample(term,(argc > 2) ? args[2] : 0);
	term.printf("invalid argument\n");
	return 1;
}


#elif defined CONFIG_IDF_TARGET_ESP8266
static IntSignal *Adc = 0;


static void adc_sample_cb(void *arg = 0)
{
	uint16_t adc;
	adc_read(&adc);
	Adc->setValue(adc);
}


int adc_sample(Terminal &term)
{
	if (Adc == 0) {
		term.println("not initialized");
		return 1;
	}
	adc_sample_cb();
	term.printf("%u\n",Adc->getValue());
	return 0;
}


static int adc_print(Terminal &term)
{
	if (Adc == 0) {
		term.println("not initialized");
		return 1;
	}
	term.printf("%u\n",Adc->getValue());
	return 0;
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
		log_warn(TAG,"need adc name");
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
		log_error(TAG,"adc init failed: %s",esp_err_to_name(e));
		return 1;
	}
	const char *n = HWConf.adc().adc_name().c_str();
	Adc = new IntSignal(n);
	action_add(concat(n,"!sample"),adc_sample_cb,0,"take an ADC sample");
	return 0;
}


int adc(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		term.printf("adc is %sinitialized\n",Adc ? "" : "not ");
		return 0;
	}
	if (!strcmp(args[1],"init")) {
		if (term.getPrivLevel() == 0) {
			term.println("Access denied.");
			return 1;
		}
		return adc_setup();
	}
	if (!strcmp(args[1],"print"))
		return adc_print(term);
	if (!strcmp(args[1],"sample"))
		return adc_sample(term);
	term.println("invalid argument");
	return 1;
}

#else	// other IDF
#error unknown target
#endif
