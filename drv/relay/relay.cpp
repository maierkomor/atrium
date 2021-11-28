/*
 *  Copyright (C) 2017-2021, Thomas Maier-Komor
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


#include "actions.h"
#include "event.h"
#include "log.h"
#include "relay.h"

#include <freertos/semphr.h>

#include <driver/gpio.h>
#include <esp_timer.h>
#include <string.h>

#if defined ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif


Relay *Relay::Relays = 0;
static SemaphoreHandle_t Mtx = 0;
#define TAG MODULE_RELAY


static void relay_turn_on(void *R)
{
	((Relay *)R)->set(true);
}


static void relay_turn_off(void *R)
{
	((Relay *)R)->set(false);
}


static void relay_toggle(void *R)
{
	((Relay *)R)->toggle();
}


Relay::Relay(const char *name, gpio_num_t gpio, uint32_t minitv, bool onlvl)
: m_name(name)
, m_tlt(0)
, m_minitv(minitv + 1) // must not be 0, interval should be at least minitv - so+1
, m_gpio(gpio)
, m_onlvl(onlvl)
, m_cb(0)
{
	if (0 == Mtx)
		Mtx = xSemaphoreCreateMutex();
	xSemaphoreTake(Mtx,portMAX_DELAY);
	m_tmr = xTimerCreate(name,pdMS_TO_TICKS(m_minitv),false,(void*)this,timerCallback);
	gpio_pad_select_gpio(gpio);
	gpio_set_direction(gpio,GPIO_MODE_OUTPUT);
	if (char *an = concat(name,"!on"))
		action_add(an,relay_turn_on,this,"turn on");
	if (char *an = concat(name,"!off"))
		action_add(an,relay_turn_off,this,"turn off");
	if (char *an = concat(name,"!toggle"))
		action_add(an,relay_toggle,this,"toggle relay");
	m_onev = event_register(name,"`on");
	m_offev = event_register(name,"`off");
	m_changedev = event_register(name,"`changed");
	m_next = Relays;
	Relays = this;
	xSemaphoreGive(Mtx);
}


Relay *Relay::get(const char *n)
{
	Relay *r = Relays;
	while (r && strcmp(n,r->name()))
		r = r->m_next;
	return r;
}



void Relay::timerCallback(TimerHandle_t h)
{
	Relay *r = (Relay *)pvTimerGetTimerID(h);
	xSemaphoreTake(Mtx,portMAX_DELAY);
	bool o = r->m_set;
	r->sync();
	xSemaphoreGive(Mtx);
	log_dbug(TAG,"%s at gpio%d: sync %s",r->m_name,r->m_gpio,o?"on":"off");
}


void Relay::set(bool o)
{
	xSemaphoreTake(Mtx,portMAX_DELAY);
	if (o && m_interlock && (m_interlock->m_set || m_interlock->m_state)) {
		xSemaphoreGive(Mtx);
		log_dbug(TAG,"%s: set blocked, interlocked",m_name);
		return;
	}
	uint32_t now = esp_timer_get_time() / 1000;
	m_set = o;
	int dt = m_minitv - (now - m_tlt);
	const char *mode = 0;
	if (dt > 0) {
		if (pdTRUE != xTimerIsTimerActive(m_tmr)) {
			if ((pdFAIL == xTimerChangePeriod(m_tmr,dt,dt))
				|| (pdFAIL == xTimerStart(m_tmr,dt))) {
				now = esp_timer_get_time() / 1000;
				mode = "failed timer";
			} else
				mode = "timed";
		} else
			mode = "in-timer";
	}
	// test again to retry after timer failure
	if ((now - m_tlt) >= m_minitv) {
		if (mode == 0)
			mode = "sync";
		sync();
	}
	xSemaphoreGive(Mtx);
	log_dbug(TAG,"%s at gpio%d: %s turn %s",m_name,m_gpio,mode,o?"on":"off");
}


void Relay::sync()
{
	// assumption: mutex is locked
	if (m_set != m_state) {
		m_state = m_set;
			// state=on, onlvl=high, !(s^o) = 1
			// state=off, onlvl=high, !(s^o) = 0
			// state=on, onlvl=low, !(s^o) = 0
			// state=off, onlvl=low, !(s^o) = 1
		gpio_set_level(m_gpio,!(m_state^m_onlvl));
		m_cb(this);
		m_tlt = esp_timer_get_time() / 1000;
		event_trigger(m_state ? m_onev : m_offev);
		event_trigger(m_changedev);
	}
}


void Relay::turn_on()
{
	set(true);
}


void Relay::turn_off()
{
	set(false);
}


uint32_t Relay::time_on() const
{
	uint32_t now = esp_timer_get_time()/1000;
	uint32_t dt = 0;
	xSemaphoreTake(Mtx,portMAX_DELAY);
	if (m_state)
		dt = now - m_tlt;
	xSemaphoreGive(Mtx);
	return dt;
}


void Relay::toggle()
{
	set(!m_state);
}


