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
#include "env.h"
#include "event.h"
#include "globals.h"
#include "hlw8012.h"
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

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

#define TAG MODULE_GPIO


#if defined ESP32 && ! defined CONFIG_IOEXTENDERS
static const char *GpioIntrTypeStr[] = {
	"disabled",
	"enabled",
	"NMI",
	"NMI and regular",
};
#endif


#ifdef CONFIG_GPIOS
class Gpio
{
	public:
	Gpio(const GpioConfig &);

	Gpio(const char *name, xio_t gpio, unsigned config)
	: m_env(name,false)
	, m_gpio(gpio)
	, m_intrev(0)
	{
		init(config);
	}

	static void set(void *);

	static Gpio *get(const char *);

	int get_lvl()
	{
		int lvl = xio_get_lvl(m_gpio);
		m_env.set(lvl != 0);
		return lvl;
	}

	const char *name() const
	{ return m_env.name(); }

	void set_lvl(int lvl)
	{ xio_set_lvl(m_gpio,(xio_lvl_t)lvl); }

	void attach(EnvObject *r)
	{ r->add(&m_env); }

	static void isr_update(Gpio *o)
	{ o->m_env.set(o->m_intlvl); }

	xio_t get_xio() const
	{ return m_gpio; }

	private:
	static void isr_handler(void *);
	void init(unsigned);
	static void action_sample(void *);
	static void action_set0(void *);
	static void action_set1(void *);
	static void action_toggle(void *);

	EnvBool m_env;
	xio_t m_gpio;
	Gpio *m_next;			// set in init()
	bool m_intlvl;			// level at time of interrupt
	event_t m_intrev;
	static Gpio *First;
};


Gpio *Gpio::First = 0;


Gpio::Gpio(const GpioConfig &c)
: m_env(c.name().c_str(),false)
, m_gpio((xio_t) c.gpio())
, m_intrev(0)
{
	init(c.config());
}


Gpio *Gpio::get(const char *n)
{
	Gpio *r = First;
	while (r && strcmp(r->m_env.name(),n))
		r = r->m_next;
	return r;
}


void Gpio::set(void *arg)
{
	if (arg == 0)
		return;
	const char *n = (const char *) arg;
	const char *c = strchr(n,':');
	if ((c == 0) || (c[2] != 0))
		return;
	Gpio *x = Gpio::get(n);
	if (x == 0) {
		log_dbug(TAG,"unknown gpio %s",n);
		return;
	}
	xio_cfg_t cfg;
	switch (c[1]) {
	case '0':
		xio_set_lvl(x->m_gpio,xio_lvl_0);
		break;
	case '1':
		xio_set_lvl(x->m_gpio,xio_lvl_1);
		break;
	case 'z':
		xio_set_lvl(x->m_gpio,xio_lvl_hiz);
		break;
	case 'i':
		cfg = XIOCFG_INIT;
		cfg.cfg_io = xio_cfg_io_in;
		xio_config(x->m_gpio,cfg);
		break;
	case 'o':
		cfg = XIOCFG_INIT;
		cfg.cfg_io = xio_cfg_io_out;
		xio_config(x->m_gpio,cfg);
		break;
	case 't':
		xio_set_lvl(x->m_gpio,(xio_lvl_t)(xio_get_lvl(x->m_gpio)^1));
		break;
	default:
		;
	}
}


void Gpio::action_sample(void *arg)
{
	Gpio *gpio = (Gpio *)arg;
	int lvl = gpio->get_lvl();
	log_dbug(TAG,"%s = %d",gpio->name(),lvl);
}


void Gpio::action_set0(void *arg)
{
	Gpio *gpio = (Gpio *)arg;
	xio_set_lo(gpio->m_gpio);
	log_dbug(TAG,"%s <= 0",gpio->name());
}


void Gpio::action_set1(void *arg)
{
	Gpio *gpio = (Gpio *)arg;
	xio_set_hi(gpio->m_gpio);
	log_dbug(TAG,"%s <= 1",gpio->name());
}


void Gpio::action_toggle(void *arg)
{
	Gpio *gpio = (Gpio *)arg;
	unsigned lvl = xio_get_lvl(gpio->m_gpio)^1;
	xio_set_lvl(gpio->m_gpio,(xio_lvl_t)lvl);
	log_dbug(TAG,"%s <= %d",gpio->name(),lvl);
}


void Gpio::init(unsigned config)
{
	log_info(TAG,"gpio%d named %s, config 0x%x",m_gpio,m_env.name(),config);
	m_next = First;
	First = this;
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = (xio_cfg_io_t)((config & 0x3));
	cfg.cfg_intr = (xio_cfg_intr_t)(((config >> 2) & 0x7));
	if (config & (1<<7))
		cfg.cfg_pull = xio_cfg_pull_up;
	else if (config & (1<<8))
		cfg.cfg_pull = xio_cfg_pull_down;
	if (config & (1<<5)) {	// set-init?
		bool inithi = config & (1 << 6);
		cfg.cfg_initlvl = inithi ? xio_cfg_initlvl_high : xio_cfg_initlvl_low;
		log_dbug(TAG,"init %s",inithi ? "high" : "low");
	}
	if (0 > xio_config(m_gpio,cfg)) {
		log_warn(TAG,"config gpio %u failed",m_gpio);
		return;
	}
	const char *name = m_env.name();
	Action *a = 0;
	if ((config & 0x3) == 0) {	// input
		a = action_add(concat(name,"!sample"),Gpio::action_sample,(void*)this,"sample gpio input");
	} else if (config & 0x3) {	// not input, not disabled
		// configured as output
		log_dbug(TAG,"gpio output actions");
		action_add(concat(name,"!set_1"),Gpio::action_set1,(void*)this,"set gpio high");
		action_add(concat(name,"!set_0"),Gpio::action_set0,(void*)this,"set gpio low");
		action_add(concat(name,"!toggle"),Gpio::action_toggle,(void*)this,"toggle gpio");
	}
	if ((config >> 2) & 0x3) {
		// configure interrupts
		log_dbug(TAG,"gpio interrupts");
		m_intrev = event_register(concat(name,"`intr"));
		if (a == 0)
			log_warn(TAG,"gpio%d: interrupts only work on inputs",m_gpio);
		else if (xio_set_intr(m_gpio,isr_handler,this))
			log_warn(TAG,"failed to add ISR for gpio%d",m_gpio);
		else if (0 == event_callback(m_intrev,a))
			log_warn(TAG,"failed to add ISR update handler for gpio%d",m_gpio);
//		else if (xio_intr_enable(m_gpio))
//			log_warn(TAG,"failed to enable ISR on gpio%d",m_gpio);
		else
			log_info(TAG,"enabled interrupts on gpio%d",m_gpio);
	}
}


void Gpio::isr_handler(void *arg)
{
	Gpio *g = (Gpio *)arg;
	int lvl = xio_get_lvl(g->m_gpio);
	g->m_intlvl = (lvl != 0);
	event_isr_trigger(g->m_intrev);
}
#endif


#ifdef ESP32
void esp32_gpio_status(Terminal &term, gpio_num_t gpio)
{
//	uint32_t iomux = read_iomux_conf(gpio);
/*
	uint32_t iomux = REG_READ(GPIO_PIN_MUX_REG[gpio]);
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
	*/
}
#endif


const char *gpio(Terminal &term, int argc, const char *args[])
{
#ifdef CONFIG_IOEXTENDERS
	if (argc == 1) {
		const char *dir[] = {"in: 0","in: 1","out","od"};
		const char *out[] = {"","out: lo","out: hi"};
		XioCluster **cl = XioCluster::getClusters();
		uint8_t num = XioCluster::numClusters();
		term.printf("%u io clusters\n",num);
		XioCluster **e = cl + num;
		while (cl != e) {
			XioCluster *c = *cl++;
			unsigned gpio = c->getBase();
			const char *n = c->getName();
			unsigned num = c->numIOs();
			assert(n);
			term.printf("cluster %s: %u IOs\n",n,num);
			for (int i = 0; i < num; ++i) {
				int d = c->get_dir(i);
				int o = d == 2 ? c->get_out(i) : -1;
				++o;
				if (d != -1) {
					if (d)
						++d;
					else
						d = c->get_lvl(i);
					assert(dir[d]);
					term.printf("%2u (%s/%d): %s%s\n",gpio,n,i,dir[d],out[o]);
				}
				++gpio;
			}
		}
		return 0;
	}
	char *e;
	long l = strtol(args[1],&e,0);
	if ((*e) || (l < 0))
		return "Invalid argument #1.";
	if (argc == 2) {
		int r = xio_get_lvl(l);
		if (r < 0)
			return "Failed.";
		int d = xio_get_dir(l);
		const char *dir = (d < 0) ? "unknown" : GpioDirStr[d];
		XioCluster *c = XioCluster::getCluster(l);
		term.printf("%d (%s/%d) %s %u\n",(int)l,c->getName(),(int)(l-c->getBase()),dir,r);
#ifdef ESP32
		if (l < 48)
			esp32_gpio_status(term,(gpio_num_t)l);
#endif
		return 0;
	} 
	xio_cfg_t cfg = XIOCFG_INIT;
	if (argc == 3) {
		if (!strcmp(args[2],"0"))
			return xio_set_lo(l) ? "Failed." : 0;
		else if (!strcmp(args[2],"1"))
			return xio_set_hi(l) ? "Failed." : 0;
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
			return "Invalid argument #2.";
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
				return "Invalid argument #3.";
		}
	}
	int r = xio_config(l,cfg);
	if (r == -1)
		return "Failed.";
	if (r == 0)
		term.println("no-pull");
	if (r & xio_cap_pullup)
		term.println("pullup");
	if (r & xio_cap_pulldown)
		term.println("pulldown");
	if (r & xio_cap_od)
		term.println("open-drain");
	return 0;
#elif defined ESP32
	if (argc == 1) {
		/*
		uint64_t enabled = GPIO_ENABLE_REG | ((uint64_t)GPIO_ENABLE1_REG << 32);
		for (unsigned pin = 0; pin < 32; ++pin)
			term.printf("pin %2u: gpio 0x%08x=%08x, %s\n"
				,pin
				,GPIO_IN_REG(pin)
				,*(uint32_t*)GPIO_IN_REG(pin)
				,((enabled>>pin)&1) ? "enabled" : "disabled"
				);
		*/
	} else if ((argc == 2) && (args[1][0] >= '0') && (args[1][0] <= '9')) {
		long pin = strtol(args[1],0,0);
		if (!GPIO_IS_VALID_GPIO(pin)) {
			return "Invalid argument #1.";
		}
//		uint32_t iomux = read_iomux_conf(pin);
		uint32_t iomux = REG_READ(GPIO_PIN_MUX_REG[pin]);
		uint32_t gpiopc = 0;//*(uint32_t*)GPIO_REG(pin);	// pin configuration @0x88+4*n
		bool level = gpio_get_level((gpio_num_t)pin);
//		if (pin < 32) 
//			level = (((iomux>>9)&1 ? GPIO_IN_REG : GPIO_OUT_REG) >> pin ) & 1;
//		else
//			level = (((iomux>>9)&1 ? GPIO_IN1_REG : GPIO_OUT1_REG) >> (pin-32) ) & 1;
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
		return "Invalid argument #1.";
	}
#elif defined CONFIG_IDF_TARGET_ESP8266
	if (argc > 3)
		return "Invalid number of arguments.";
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
				return "Invalid argument #2.";
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
				return "Invalid argument #2.";
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
			return "Invalid argument #1.";
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
				return "Invalid argument #2.";
			if (e != 0) {
				return esp_err_to_name(e);
			}
		}
	} else {
		return "Invalid argument #1.";
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


#if defined CONFIG_GPIOS && defined CONFIG_LUA
static int luax_gpio_set(lua_State *L)
{
	int v = luaL_checkinteger(L,2);
	if ((v < 0) || (v > 1)) {
		lua_pushfstring(L,"gpio_set(%d)",v);
		lua_error(L);
	}
	const char *n = luaL_checkstring(L,1);
	Gpio *g = Gpio::get(n);
	if (g == 0) {
		lua_pushfstring(L,"gpio_set(%s)",n);
		lua_error(L);
	}
	log_dbug(TAG,"gpio %d <= %d",g->get_xio(),v);
	g->set_lvl((xio_lvl_t)v);
	return 0;
}


static int luax_gpio_get(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	Gpio *g = Gpio::get(n);
	if (g == 0) {
		lua_pushfstring(L,"gpio_get(%s)",n);
		lua_error(L);
	}
	int lvl = g->get_lvl();
	lua_pushinteger(L,lvl);
	return 1;
}


static LuaFn Functions[] = {
	{ "gpio_set", luax_gpio_set, "set gpio level" },
	{ "gpio_get", luax_gpio_get, "get gpio level" },
	{ 0, 0, 0 }
};
#endif


void xio_setup()
{
#ifdef CONFIG_IOEXTENDERS
	// first assign configured clusters
	for (const auto &c : HWConf.iocluster()) {
		const char *n = c.name().c_str();
		if (XioCluster *f = XioCluster::getInstance(n)) {
			uint8_t b = c.base();
			if (b && (f->numIOs() == c.numio()))
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
#ifdef CONFIG_GPIOS
	for (const auto &c : HWConf.gpios()) {
		const char *n = c.name().c_str();
		int8_t gpio = c.gpio();
		if ((gpio == -1) || (n[0] == 0))
			continue;
		Gpio *dev = new Gpio(n,(xio_t)gpio,c.config());
		dev->attach(RTData);
	}
	action_add("gpio!set",Gpio::set,0,"set gpio <name>:<value>, with value=0,1,z,i,o,t");
#endif
#if defined CONFIG_GPIOS && defined CONFIG_LUA
	xlua_add_funcs("gpio",Functions);
#endif
#ifdef CONFIG_HLW8012
	if (HWConf.has_hlw8012()) {
		const auto &c = HWConf.hlw8012();
		HLW8012 *dev = HLW8012::create(c.sel(),c.cf(),c.cf1());
		dev->attach(RTData);
	}
#endif
}
