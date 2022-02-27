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
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "terminal.h"
#include "xio.h"
#include "coreio.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#ifdef CONFIG_IDF_TARGET_ESP8266
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


static const char *PullStr[] = {
	"none",
	"pull-up",
	"pull-down",
	"pull-updown",
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

	Gpio(const char *name, xio_t gpio, unsigned config)
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
	xio_t m_gpio;
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
, m_gpio((xio_t) c.gpio())
#ifdef CONFIG_SIGNAL_PROC
, m_sig(0)
#endif
, m_intrev(0)
{
	init(c.config());
}


static void gpio_action_set0(void *arg)
{
	xio_t gpio = (xio_t)(unsigned)arg;
	xio_set_lo(gpio);
	log_dbug(TAG,"gpio%d <= 0",gpio);
}


static void gpio_action_set1(void *arg)
{
	xio_t gpio = (xio_t)(unsigned)arg;
	xio_set_hi(gpio);
	log_dbug(TAG,"gpio%d <= 1",gpio);
}


static void gpio_action_toggle(void *arg)
{
	unsigned v = (unsigned)arg;
	xio_t gpio = (xio_t)(unsigned)v;
	unsigned lvl = xio_get_lvl(gpio)^1;
	xio_set_lvl(gpio,(xio_lvl_t)lvl);
	log_dbug(TAG,"gpio%d <= %d",gpio,lvl);
}


void Gpio::action_sample(void *arg)
{
	Gpio *g = (Gpio*)arg;
	int lvl = xio_get_lvl(g->m_gpio);
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
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = (xio_cfg_io_t)((config & 0x3));
	cfg.cfg_intr = (xio_cfg_intr_t)(((config >> 2) & 0x3));
	cfg.cfg_pull = (xio_cfg_pull_t)(((config >> 7) & 3) + 1);
	if (xio_config(m_gpio,cfg)) {
		log_warn(TAG,"config gpio %u failed",m_gpio);
		return;
	}
	if (config & (1<<5)) {
		unsigned lvl = (config>>6)&0x1;
		log_dbug(TAG,"set level %d",lvl);
		if (xio_set_lvl(m_gpio,(xio_lvl_t)lvl))
			log_warn(TAG,"set level %d on gpio %d failed",lvl,m_gpio);
	}
	if (config & 0x3) {	// not input
		// configured as output
		log_dbug(TAG,"gpio output actions");
		action_add(concat(m_name,"!set_1"),gpio_action_set1,(void*)(unsigned)m_gpio,"set gpio high");
		action_add(concat(m_name,"!set_0"),gpio_action_set0,(void*)(unsigned)m_gpio,"set gpio low");
		action_add(concat(m_name,"!toggle"),gpio_action_toggle,(void*)(unsigned)m_gpio,"toggle gpio");
	}
	if ((config >> 2) & 0x3) {
		// configure interrupts
		log_dbug(TAG,"gpio interrupts");
		m_intrev = event_register(concat(m_name,"`intr"));
#ifdef CONFIG_SIGNAL_PROC
		m_sig = new IntSignal(m_name);
#endif
		action_add(concat(m_name,"!sample"),action_sample,(void*)this,"sample gpio");
		if (xio_set_intr(m_gpio,isr_handler,this))
			log_warn(TAG,"add ISR for gpio%d",m_gpio);
	}
}


void Gpio::isr_handler(void *arg)
{
	Gpio *g = (Gpio *)arg;
	int lvl = xio_get_lvl(g->m_gpio);
#ifdef CONFIG_SIGNAL_PROC
	assert(g->m_sig);
	g->m_sig->setValue(lvl);
#else
	g->m_intlvl = lvl;
#endif
	event_isr_trigger(g->m_intrev);
}


#ifdef CONFIG_IDF_TARGET_ESP32
void esp32_gpio_status(Terminal &term, gpio_num_t gpio)
{
	uint32_t iomux = read_iomux_conf(gpio);
	uint32_t gpiopc = *(uint32_t*)GPIO_REG(gpio);	// pin configuration @0x88+4*n
	bool level;
	if (gpio < 32) 
		level = (((iomux>>9)&1 ? GPIO_IN_REG : GPIO_OUT_REG) >> gpio ) & 1;
	else
		level = (((iomux>>9)&1 ? GPIO_IN1_REG : GPIO_OUT1_REG) >> (gpio-32) ) & 1;
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
		,gpio,iomux,gpiopc
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
#endif


int gpio(Terminal &term, int argc, const char *args[])
{
#ifdef CONFIG_IOEXTENDERS
	if (argc == 1) {
		unsigned gpio = 0;
		XioCluster **cl = XioCluster::getClusters();
		uint8_t num = XioCluster::numClusters();
		term.printf("%u io clusters\n",num);
		XioCluster **e = cl + num;
		while (cl != e) {
			XioCluster *c = *cl++;
			const char *n = c->getName();
			term.printf("cluster %s: %u IOs\n",n,c->numIOs());
			for (int i = 0; i < c->numIOs(); ++i) {
				int d = c->get_dir(i);
				if (d != -1)
					term.printf("%u (%s/%d): %s\n",gpio,n,i,d == 0?"in":(d==1?"out":"od"));
				++gpio;
			}
		}
		return 0;
	}
	char *e;
	long l = strtol(args[1],&e,0);
	if ((*e) || (l < 0))
		return arg_invalid(term,args[1]);
	if (argc == 2) {
		int r = xio_get_lvl(l);
		if (r < 0)
			return -1;
		int d = xio_get_dir(l);
		const char *dir = (d < 0) ? "unknown" : GpioDirStr[d];
		XioCluster *c = XioCluster::getCluster(l);
		term.printf("%d (%s/%d) %s %u\n",(int)l,c->getName(),(int)(l-c->getBase()),dir,r);
#ifdef CONFIG_IDF_TARGET_ESP32
		if (l < 48)
			esp32_gpio_status(term,(gpio_num_t)l);
#endif
		return 0;
	} 
	xio_cfg_t cfg = XIOCFG_INIT;
	if (argc == 3) {
		if (!strcmp(args[2],"0"))
			return xio_set_lo(l);
		else if (!strcmp(args[2],"1"))
			return xio_set_hi(l);
		else if (!strcmp(args[2],"in"))
			cfg.cfg_io = xio_cfg_io_in;
		else if (!strcmp(args[2],"out"))
			cfg.cfg_io = xio_cfg_io_out;
		else if (!strcmp(args[2],"od"))
			cfg.cfg_io = xio_cfg_io_od;
		else if (!strcmp(args[2],"pullup"))
			cfg.cfg_pull = xio_cfg_pull_up;
		else if (!strcmp(args[2],"pulldown"))
			cfg.cfg_pull = xio_cfg_pull_down;
		else if (!strcmp(args[2],"flags"))
			;	// explicitly do nothing
		else
			return arg_invalid(term,args[2]);
	}
	if (argc == 4) {
		if (!strcmp(args[2],"pull")) {
			if (!strcmp(args[3],"up"))
				cfg.cfg_pull = xio_cfg_pull_up;
			else if (!strcmp(args[3],"down"))
				cfg.cfg_pull = xio_cfg_pull_down;
			else if (!strcmp(args[3],"updown"))
				cfg.cfg_pull = xio_cfg_pull_updown;
			else if (!strcmp(args[3],"off"))
				cfg.cfg_pull = xio_cfg_pull_none;
			else if (!strcmp(args[3],"none"))
				cfg.cfg_pull = xio_cfg_pull_none;
			else
				return arg_invalid(term,args[3]);
		}
	}
	int r = xio_config(l,cfg);
	if (r == -1)
		return -1;
	if (r == 0)
		term.println("none");
	if (r & xio_cap_pullup)
		term.println("pullup");
	if (r & xio_cap_pulldown)
		term.println("pulldown");
	if (r & xio_cap_od)
		term.println("open-drain");
	return 0;
#elif defined CONFIG_IDF_TARGET_ESP32
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
	} else {
		return arg_invalid(term,args[1]);
	}
#elif defined CONFIG_IDF_TARGET_ESP8266
	if (argc > 3)
		return arg_invnum(term);
	if (argc == 1) {
		uint32_t dir = GPIO_REG_READ(GPIO_ENABLE_ADDRESS);
		for (int p = 0; p <= 16; ++p ) {
			uint8_t v;
			char d;
			if (dir & (1<<p)) {
				v = ((GPIO.out>>p)&1);
				d = 'O';
			} else {
				v = ((GPIO.in>>p)&1);
				d = 'I';
			}
			term.printf("pin %2d: %c:%c\n",p,d,'0'+v);
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
			term.printf("%d",xio_get_lvl((xio_t)l));
		} else if (argc == 3) {
			esp_err_t e;
			xio_cfg_t cfg = XIOCFG_INIT;
			if (0 == strcmp(args[2],"out")) {
				cfg.cfg_io = xio_cfg_io_out;
				e = xio_config((xio_t)l,cfg);
			} else if (0 == strcmp(args[2],"in")) {
				cfg.cfg_io = xio_cfg_io_in;
				e = xio_config((xio_t)l,cfg);
			} else if ((0 == strcmp(args[2],"0")) || (0 == strcmp(args[2],"off")))
				e = xio_set_lo((xio_t)l);
			else if ((0 == strcmp(args[2],"1")) || (0 == strcmp(args[2],"on")))
				e = xio_set_hi((xio_t)l);
			else
				return arg_invalid(term,args[2]);
			if (e != 0) {
				term.println(esp_err_to_name(e));
				return 1;
			}
		}
	} else {
		return arg_invalid(term,args[1]);
	}
#else
#error unknwon target
#endif
	return 0;
}


int gpio_setup()
{
	coreio_register();
	return 0;
}


int xio_setup()
{
#ifdef CONFIG_IOEXTENDERS
	// first assign configured clusters
	for (const auto &c : HWConf.iocluster()) {
		const char *n = c.name().c_str();
		if (XioCluster *f = XioCluster::getInstance(n)) {
			uint8_t b = c.base();
			if (b && (f->numIOs()== c.numio()))
				f->attach(b);
			if (c.has_int_a()  && f->set_intr_a(c.int_a()))
				log_warn(TAG,"failed to attach %s to %u as intr_a",n,c.int_a());
			if (c.has_int_b() && f->set_intr_b(c.int_b()))
				log_warn(TAG,"failed to attach %s to %u as intr_b",n,c.int_a());
		}
	}
	// then assign unconfigured clusters
	XioCluster **funcs = XioCluster::getClusters();
	uint8_t num = XioCluster::numClusters();
	for (uint8_t n = 0; n < num; ++n) {
		XioCluster *func = *funcs;
		if (func->getBase() == -1) {
			func->attach(0);
			GpioCluster *c = HWConf.add_iocluster();
			c->set_name(func->getName());
			c->set_base(func->getBase());
			c->set_numio(func->numIOs());
			log_dbug(TAG,"add config for I/O cluster %s: %u..%u",func->getName(),func->getBase(),func->numIOs());
		}
		++funcs;
	}
#endif
	for (const auto &c : HWConf.gpio()) {
		const char *n = c.name().c_str();
		int8_t gpio = c.gpio();
		if ((gpio == -1) || (n[0] == 0))
			continue;
		new Gpio(n,(xio_t)gpio,c.config());
		
	}
	return 0;
}
