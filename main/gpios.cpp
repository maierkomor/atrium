/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "terminal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#ifdef ESP8266
#include <rom/gpio.h>
#include <esp8266/gpio_struct.h>
#endif


#define TAG MODULE_GPIO

#ifdef CONFIG_IDF_TARGET_ESP32
static const char *GpioIntrTypeStr[] = {
	"disabled",
	"enabled",
	"NMI",
	"NMI and regular",
};


static const char *GpioIntrTriggerStr[] = {
	"disabled",
	"falling edge",
	"raising edge",
	"any edge",
	"low level",
	"high level",
};


static uint32_t read_iomux_conf(unsigned io)
{
	switch (io) {
	case 0: return *(uint32_t *)GPIO_PIN_REG_0;
	case 1: return *(uint32_t *)GPIO_PIN_REG_1;
	case 2: return *(uint32_t *)GPIO_PIN_REG_2;
	case 3: return *(uint32_t *)GPIO_PIN_REG_3;
	case 4: return *(uint32_t *)GPIO_PIN_REG_4;
	case 5: return *(uint32_t *)GPIO_PIN_REG_5;
	case 6: return *(uint32_t *)GPIO_PIN_REG_6;
	case 7: return *(uint32_t *)GPIO_PIN_REG_7;
	case 8: return *(uint32_t *)GPIO_PIN_REG_8;
	case 9: return *(uint32_t *)GPIO_PIN_REG_9;
	case 10: return *(uint32_t *)GPIO_PIN_REG_10;
	case 11: return *(uint32_t *)GPIO_PIN_REG_11;
	case 12: return *(uint32_t *)GPIO_PIN_REG_12;
	case 13: return *(uint32_t *)GPIO_PIN_REG_13;
	case 14: return *(uint32_t *)GPIO_PIN_REG_14;
	case 15: return *(uint32_t *)GPIO_PIN_REG_15;
	case 16: return *(uint32_t *)GPIO_PIN_REG_16;
	case 17: return *(uint32_t *)GPIO_PIN_REG_17;
	case 18: return *(uint32_t *)GPIO_PIN_REG_18;
	case 19: return *(uint32_t *)GPIO_PIN_REG_19;
//	case 20: return *(uint32_t *)GPIO_PIN_REG_20;
	case 21: return *(uint32_t *)GPIO_PIN_REG_21;
	case 22: return *(uint32_t *)GPIO_PIN_REG_22;
	case 23: return *(uint32_t *)GPIO_PIN_REG_23;
//	case 24: return *(uint32_t *)GPIO_PIN_REG_24;
	case 25: return *(uint32_t *)GPIO_PIN_REG_25;
	case 26: return *(uint32_t *)GPIO_PIN_REG_26;
	case 27: return *(uint32_t *)GPIO_PIN_REG_27;
//	case 28: return *(uint32_t *)GPIO_PIN_REG_28;
//	case 29: return *(uint32_t *)GPIO_PIN_REG_29;
//	case 30: return *(uint32_t *)GPIO_PIN_REG_30;
//	case 31: return *(uint32_t *)GPIO_PIN_REG_31;
	case 32: return *(uint32_t *)GPIO_PIN_REG_32;
	case 33: return *(uint32_t *)GPIO_PIN_REG_33;
	case 34: return *(uint32_t *)GPIO_PIN_REG_34;
	case 35: return *(uint32_t *)GPIO_PIN_REG_35;
	case 36: return *(uint32_t *)GPIO_PIN_REG_36;
	case 37: return *(uint32_t *)GPIO_PIN_REG_37;
	case 38: return *(uint32_t *)GPIO_PIN_REG_38;
	case 39: return *(uint32_t *)GPIO_PIN_REG_39;
	default:
		return 0xffffffff;
	}
}
#endif


class Gpio
{
	public:
	Gpio(const GpioConfig &);

	Gpio(const char *name, gpio_num_t gpio, unsigned config)
	: m_name(name)
	, m_gpio(gpio)
#ifdef CONFIG_SIGNAL_PROC
	, m_sig(0)
#endif
	, m_intrev(0)
	{
		init(config);
	}

	private:
	static void isr_handler(void *);
	void init(unsigned);

	const char *m_name;
	gpio_num_t m_gpio;
	Gpio *m_next;
#ifdef CONFIG_SIGNAL_PROC
	IntSignal *m_sig;
#else
	bool m_intlvl,m_lvl;		// level at time of interrupt
#endif
	event_t m_intrev;
	static Gpio *First;
	static void action_sample(void*);
};


Gpio *Gpio::First = 0;


Gpio::Gpio(const GpioConfig &c)
: m_name(c.name().c_str())
, m_gpio((gpio_num_t) c.gpio())
#ifdef CONFIG_SIGNAL_PROC
, m_sig(0)
#endif
, m_intrev(0)
{
	init(c.config());
}


static void gpio_action_set0(void *arg)
{
	gpio_num_t gpio = (gpio_num_t)(unsigned)arg;
	gpio_set_level(gpio,0);
	log_dbug(TAG,"gpio%d <= 0",gpio);
}


static void gpio_action_set1(void *arg)
{
	gpio_num_t gpio = (gpio_num_t)(unsigned)arg;
	gpio_set_level(gpio,1);
	log_dbug(TAG,"gpio%d <= 1",gpio);
}


static void gpio_action_toggle(void *arg)
{
	unsigned v = (unsigned)arg;
	gpio_num_t gpio = (gpio_num_t)v;
	unsigned lvl = gpio_get_level(gpio)^1;
	gpio_set_level(gpio,lvl);
	log_dbug(TAG,"gpio%d <= %d",gpio,lvl);
}


void Gpio::action_sample(void *arg)
{
	Gpio *g = (Gpio*)arg;
	int lvl = gpio_get_level(g->m_gpio);
#ifdef CONFIG_SIGNAL_PROC
	g->m_sig->setValue(lvl);
#else
	g->m_lvl = lvl;
#endif
	log_dbug(TAG,"gpio%d = %d",g->m_gpio,lvl);
}


void Gpio::init(unsigned config)
{
	log_info(TAG,"gpio%d named %s",m_gpio,m_name);
	m_next = First;
	First = this;
	gpio_int_type_t intr = (gpio_int_type_t)((config >> 2) & 0x3);
	gpio_mode_t mode = (gpio_mode_t)(config & 0x3);
	if (esp_err_t e = gpio_set_direction(m_gpio,mode))
		log_error(TAG,"set direction on gpio%d: %s",m_gpio,esp_err_to_name(e));
	if (config & (1<<5)) {
		unsigned lvl = (config>>6)&0x1;
		log_dbug(TAG,"set level %d",lvl);
		if (esp_err_t e = gpio_set_level(m_gpio,lvl))
			log_error(TAG,"cannot set initlevel %d on gpio %d: %s",lvl,m_gpio,esp_err_to_name(e));
	}
	if (config & (1<<7)) {
		log_dbug(TAG,"gpio%d: enable pull-up",m_gpio);
		gpio_pullup_en(m_gpio);
	} else {
		gpio_pullup_dis(m_gpio);
		if (config & (1<<8)) {
			log_dbug(TAG,"gpio%d: enable pull-down",m_gpio);
			gpio_pulldown_en(m_gpio);
		} else {
			gpio_pulldown_dis(m_gpio);
		}
	}
	if ((mode == GPIO_MODE_OUTPUT) || (mode == GPIO_MODE_OUTPUT_OD)) {
		log_dbug(TAG,"gpio output actions");
		action_add(concat(m_name,"!set_1"),gpio_action_set1,(void*)(unsigned)m_gpio,"set gpio high");
		action_add(concat(m_name,"!set_0"),gpio_action_set0,(void*)(unsigned)m_gpio,"set gpio low");
		action_add(concat(m_name,"!toggle"),gpio_action_toggle,(void*)(unsigned)m_gpio,"toggle gpio");
	} else if (GPIO_INTR_DISABLE != intr) {
		log_dbug(TAG,"gpio input actions");
		m_intrev = event_register(concat(m_name,"`intr"));
		if (esp_err_t e = gpio_set_intr_type(m_gpio,intr))
			log_error(TAG,"gpio%d - cannot set interrupt: %s",m_gpio,esp_err_to_name(e));
		else
			log_dbug(TAG,"gpio%d: set intr OK",m_gpio);
#ifdef CONFIG_SIGNAL_PROC
		m_sig = new IntSignal(m_name);
#endif
		action_add(concat(m_name,"!sample"),action_sample,(void*)this,"sample gpio");
		if (esp_err_t e = gpio_isr_handler_add(m_gpio,isr_handler,this))
			log_error(TAG,"add ISR for gpio%d: %s",m_gpio,esp_err_to_name(e));
	}
}


void Gpio::isr_handler(void *arg)
{
	Gpio *g = (Gpio *)arg;
	int lvl = gpio_get_level(g->m_gpio);
#ifdef CONFIG_SIGNAL_PROC
	assert(g->m_sig);
	g->m_sig->setValue(lvl);
#else
	g->m_intlvl = lvl;
#endif
	event_isr_trigger(g->m_intrev);
}


int gpio(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return arg_invnum(term);
#ifdef CONFIG_IDF_TARGET_ESP32
	if (argc == 1) {
		uint64_t enabled = GPIO_ENABLE_REG | ((uint64_t)GPIO_ENABLE1_REG << 32);
		for (unsigned pin = 0; pin < 32; ++pin)
			term.printf("pin %2u: gpio 0x%08x=%08x, %s\n"
				,pin
				,GPIO_REG(pin)
				,*(uint32_t*)GPIO_REG(pin)
				,((enabled>>pin)&1) ? "enabled" : "disabled"
				);
	} else if ((argc == 2) && (args[1][0] >= '0') && (args[1][0] <= '9')) {
		long pin = strtol(args[1],0,0);
		if (!GPIO_IS_VALID_GPIO(pin)) {
			term.printf("no gpio %ld\n",pin);
			return 1;
		}
		uint32_t iomux = read_iomux_conf(pin);
		uint32_t gpiopc = *(uint32_t*)GPIO_REG(pin);	// pin configuration @0x88+4*n
		bool level;
		if (pin < 32) 
			level = (((iomux>>9)&1 ? GPIO_IN_REG : GPIO_OUT_REG) >> pin ) & 1;
		else
			level = (((iomux>>9)&1 ? GPIO_IN1_REG : GPIO_OUT1_REG) >> (pin-32) ) & 1;
		term.printf(
			"pin %2u: iomux 0x%08x, gpiopc 0x%08x\n"
			"\tfunction   %d\n"
			"\tpad driver %d\n"
			"\tinput      %s\n"
			"\tpull-up    %s\n"
			"\tpull-down  %s\n"
			"\tlevel      %s\n"
			"\tAPP intr   %s\n"
			"\tPRO intr   %s\n"
			"\twakeup     %s\n"
			"\tinterrupts %s\n"
			,pin,iomux,gpiopc
			,((iomux>>12)&7)
			,((iomux>>10)&3)
			,((iomux>>9)&1) ? "enabled" : "disabled"
			,((iomux>>8)&1) ? "enabled" : "disabled"
			,((iomux>>7)&1) ? "enabled" : "disabled"
			,level ? "high" : "low"
			,GpioIntrTypeStr[(gpiopc>>13)&3]
			,GpioIntrTypeStr[(gpiopc>>16)&3]
			,((gpiopc>>10)&1) ? "enabled" : "disabled"
			,GpioIntrTriggerStr[(gpiopc>>7)&0x7]
			);
	}
#elif defined CONFIG_IDF_TARGET_ESP8266
	if (argc == 1) {
		uint32_t dir = GPIO_REG_READ(GPIO_ENABLE_ADDRESS);
		uint32_t in = GPIO.in;
		uint32_t out = GPIO.out;
		for (int p = 0; p <= 16; ++p ) {
			if (dir & (1<<p))
				term.printf("pin %2d: O:%c\n",p,'0'+((out>>p)&1));
			else
				term.printf("pin %2d: I:%c\n",p,'0'+((in>>p)&1));
		}
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	} else if (0 == strcmp(args[1],"scanin")) {
		// will not terminate!
		uint32_t n = UINT32_MAX;
		if (argc == 3) {
			long l = strtol(args[2],0,0);
			if (l <= 0)
				return arg_invalid(term,args[2]);
			n = l;
		}
		uint32_t g0 = GPIO.in;
		while (--n) {
			uint32_t g1 = GPIO.in;
			uint32_t d = g0 ^ g1;
			if (d) {
				con_printf("%08x",d);
				g0 = g1;
			}
			vTaskDelay(10);
		}
	} else if (0 == strcmp(args[1],"scanout")) {
		// will not terminate!
		uint32_t n = UINT32_MAX;
		if (argc == 3) {
			long l = strtol(args[2],0,0);
			if (l <= 0)
				return arg_invalid(term,args[2]);
			n = l;
		}
		uint32_t g0 = GPIO.out;
		while (--n) {
			uint32_t g1 = GPIO.out;
			uint32_t d = g0 ^ g1;
			if (d) {
				con_printf("%08x",d);
				g0 = g1;
			}
			vTaskDelay(10);
		}
#endif
	} else if ((args[1][0] >= '0') && (args[1][0] <= '9')) {
		long l = strtol(args[1],0,0);
		if ((l > 16) || (l < 0))
			return arg_invalid(term,args[1]);
		if (argc == 2) {
			term.println(gpio_get_level((gpio_num_t)l) ? "1" : "0");
		} else if (argc == 3) {
			esp_err_t e;
			if (0 == strcmp(args[2],"out")) {
				gpio_pad_select_gpio(l);
				e = gpio_set_direction((gpio_num_t)l,GPIO_MODE_OUTPUT);
			} else if (0 == strcmp(args[2],"in")) {
				gpio_pad_select_gpio(l);
				e = gpio_set_direction((gpio_num_t)l,GPIO_MODE_INPUT);
			} else if ((0 == strcmp(args[2],"0")) || (0 == strcmp(args[2],"off")))
				e = gpio_set_level((gpio_num_t)l,0);
			else if ((0 == strcmp(args[2],"1")) || (0 == strcmp(args[2],"on")))
				e = gpio_set_level((gpio_num_t)l,1);
			else
				return arg_invalid(term,args[2]);
			if (e != 0) {
				term.println(esp_err_to_name(e));
				return 1;
			}
		}
	}
#else
#error unknwon target
#endif
	else {
		return arg_invalid(term,args[1]);
	}
	return 0;
}


int gpio_setup()
{
#ifdef CONFIG_IDF_TARGET_ESP8266
	gpio_install_isr_service(0);
#else
	gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM);
#endif
	for (const auto &c : HWConf.gpio()) {
		const char *n = c.name().c_str();
		int8_t gpio = c.gpio();
		if ((gpio == -1) || (n[0] == 0))
			continue;
		if (gpio >= GPIO_NUM_MAX) {
			log_error(TAG,"no gpio%d",gpio);
			continue;
		}
		new Gpio(n,(gpio_num_t)gpio,c.config());
		
	}
	return 0;
}
