/*
 *  Copyright (C) 2018-2019, Thomas Maier-Komor
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

#ifdef CONFIG_LEDSTRIP

#include "dataflow.h"
#ifdef CONFIG_SIGNAL_PROC
#include "func.h"
#endif
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "ws2812b.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/*
#define BLACK	0X000000
#define WHITE	0xffffff
#define RED	0xff0000
#define GREEN	0x00ff00
#define BLUE	0x0000ff
#define MAGENTA	0xff00ff
#define YELLOW	0xffff00
#define CYAN	0x00ffff
*/

#define BLACK	0X000000
#define WHITE	0x202020
#define RED	0x200000
#define GREEN	0x002000
#define BLUE	0x000020
#define MAGENTA	0x200020
#define YELLOW	0x202000
#define CYAN	0x002020
#define PURPLE	0x100010

static const char TAG[] = "ledstrip";

static WS2812BDrv *LED_Strip = 0;


#if 1 // demo mode
static uint32_t ColorMap[] = {
	BLACK, WHITE, RED, GREEN, BLUE, MAGENTA, YELLOW, CYAN, PURPLE
};

static void ledstrip_task(void *arg)
{
	uint8_t numleds = (uint8_t) (unsigned) arg;
	uint8_t off = 0;
	LED_Strip->reset();
	log_dbug(TAG,"0");
	vTaskDelay(1000/portTICK_PERIOD_MS);
	LED_Strip->set_leds(WHITE);
	log_dbug(TAG,"1");
	LED_Strip->update();
	vTaskDelay(1000/portTICK_PERIOD_MS);
	LED_Strip->set_leds(BLACK);
	log_dbug(TAG,"2");
	LED_Strip->update();
	vTaskDelay(1000/portTICK_PERIOD_MS);
	for (unsigned x = 0; x < 256; ++x) {
		log_dbug(TAG,"3");
		LED_Strip->set_leds(x << 16 | x << 8 | x);
		LED_Strip->update();
		vTaskDelay(50/portTICK_PERIOD_MS);
	}
	for (;;) {
		for (int i = 0; i < numleds; ++i)
			LED_Strip->set_led(i,ColorMap[(i+off)%(sizeof(ColorMap)/sizeof(ColorMap[0]))]);
		log_dbug(TAG,"4");
		LED_Strip->update();
		if (++off == sizeof(ColorMap)/sizeof(ColorMap[0]))
			off = 0;
		vTaskDelay(1000/portTICK_PERIOD_MS);

		for (int i = 0; i < numleds; ++i)
			LED_Strip->set_led(i,1<<i);
		LED_Strip->update();
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}

#elif 0

static void execute_op(ledaction_t a, uint32_t arg)
{
	switch (a) {
	case la_nop:
		break;
	case la_disp:
		// TODO
		break;
	case la_set:
		LED_Strip->set_leds(arg>>24,arg&0xffffff);
		break;
	case la_setall:
		LED_Strip->set_leds(arg);
		break;
	case la_delay:
		vTaskDelay(arg);
		break;
		/*
	case la_setrd:
		LED_Strip->set_leds((arg<<16)&0xff0000);
		break;
	case la_setgr:
		LED_Strip->set_leds((arg<<8)&0xff00);
		break;
	case la_setbl:
		LED_Strip->set_leds((arg)&0xff);
		break;
		*/
	case la_update:
		LED_Strip->update();
		break;
	case la_fade:
		LED_Strip->update(true);
	case la_mode:
		break;
	case la_jump:
		break;
	case la_mode:
		break;
	default:
		log_warn(TAG,"unknown ledstrip command %d",a);
	}
}

static void ledstrip_task(void *arg)
{
	for (;;) {
		uint32_t r = esp_random();
		LED_Strip->set_leds(r&0xff0000);
		vTaskDelay((((r&0x1f)<<3)+20)/portTICK_PERIOD_MS);
		LED_Strip->update();
	}
}
#endif

#ifdef CONFIG_SIGNAL_PROC 
class FnLedstripSet : public Function
{
	public:
	FnLedstripSet(const char *name)
	: Function(name)
	, m_led(0)
	, m_sig(0)
	{ }

	const char *type() const
	{ return FuncName; }

	static Function *create(const char *, int argc, const char *args[]);

	int addParam(const char *);

	void operator() (DataSignal *);

	static const char FuncName[];
	
	private:
	uint8_t m_led;
	IntSignal *m_sig;
};


const char FnLedstripSet::FuncName[] = "ws2812b_set";


int FnLedstripSet::addParam(const char *p)
{
	log_dbug(TAG,"addParam(%s)",p);
	char *e;
	long l = strtol(p,&e,0);
	if (e == p) {
		DataSignal *s = DataSignal::getSignal(p);
		if (s == 0) {
			log_warn(TAG,"%s is not a signal",p);
			return 1;
		}
		IntSignal *i = s->toIntSignal();
		if (i == 0) {
			log_warn(TAG,"%s is not an integer",p);
			return 1;
		}
		i->addFunction(this);
		m_sig = i;
	} if ((l < 0) || (l > UINT8_MAX)) {
		log_warn(TAG,"value %ld out of range%s",l);
		return 1;
	} else {
		m_led = l;
	}
	return 0;
}


void FnLedstripSet::operator() (DataSignal *s)
{
	uint32_t v;
	IntSignal *i = m_sig;
	if (s != 0)
		i = s->toIntSignal();
	if (i == 0) {
		log_dbug(TAG,"no value to set");
		return;
	}
	v = i->getValue();
	log_dbug(TAG,"setting %d to 0x%x",m_led,v);
	if (m_led == 0)
		LED_Strip->set_leds(v);
	else
		LED_Strip->set_led(m_led-1,v);
	LED_Strip->update(true);
}

#endif // CONFIG_SIGNAL_PROC


int ledstrip_setup()
{
	if (!HWConf.has_ws2812b())
		return 1;
	const Ws2812bConfig &c = HWConf.ws2812b();
	if ((!c.has_gpio() || (0 == c.nleds()))) {
		log_dbug(TAG,"incomplete config");
		return 1;
	}
	log_dbug(TAG,"setup");
	unsigned nleds = c.nleds();
	LED_Strip = new WS2812BDrv;
#ifdef CONFIG_IDF_TARGET_ESP32
	if (LED_Strip->init((gpio_num_t)c.gpio(),nleds),(rmt_channel_t)c.ch())
		return 1;
#else
	if (LED_Strip->init((gpio_num_t)c.gpio(),nleds))
		return 1;
#endif
	LED_Strip->set_leds(0xffffff);
#if 1
	BaseType_t r = xTaskCreatePinnedToCore(&ledstrip_task, TAG, 4096, (void*)(unsigned)c.nleds(), 15, NULL, APP_CPU_NUM);
	if (r != pdPASS) {
		log_error(TAG,"task creation failed: %s",esp_err_to_name(r));
		return 1;
	}
#else
	/*
	char name[24];
	new FnLedstripSet("ws2812b_strip",0);
	for (unsigned l = 1; l <= nleds; ++l) {
		sprintf(name,"ws2812b_led%u",l);
		new FnLedstripSet(name,l);
	}
	*/
	new FuncFact<FnLedstripSet>;
#endif
	return 0;
}

#endif
