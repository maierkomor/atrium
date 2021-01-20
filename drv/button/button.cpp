/*
 *  Copyright (C) 2018-2021, Thomas Maier-Komor
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

#ifdef CONFIG_BUTTON

#include "button.h"
#include "event.h"
#include "log.h"

#include <driver/gpio.h>
#include <rom/gpio.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_BUTTON_GPIO < 0 || CONFIG_BUTTON_GPIO >= GPIO_PIN_COUNT
#error gpio value for button out of range
#endif


static char TAG[] = "btn";


Button::Button(const char *name, gpio_num_t gpio, gpio_pull_mode_t mode, bool active_high, Button *n)
: m_next(n)
{ 
	init(name,gpio,mode,active_high);
}


void Button::init(const char *name, gpio_num_t gpio, gpio_pull_mode_t mode, bool active_high)
{
	log_info(TAG,"button %s at gpio %u",name,gpio);
	m_name = name;
	m_gpio = gpio;
	m_presslvl = (uint8_t)active_high;
	m_pev = event_register(name,"`pressed");
	m_rev = event_register(name,"`released");
	m_sev = event_register(name,"`short");
	m_mev = event_register(name,"`med");
	m_lev = event_register(name,"`long");
	gpio_pad_select_gpio(gpio);
	if (esp_err_t e = gpio_set_direction(gpio,GPIO_MODE_INPUT))
		log_error(TAG,"GPIO %d input: %s",gpio,esp_err_to_name(e));
	if (esp_err_t e = gpio_set_pull_mode(gpio,mode))
		log_error(TAG,"pull-mode %d on GPIO %d: %s",mode,gpio,esp_err_to_name(e));
	if (esp_err_t e = gpio_set_intr_type(gpio,GPIO_INTR_ANYEDGE))
		log_error(TAG,"gpio %d edge interrupt: %s",gpio,esp_err_to_name(e));
	if (esp_err_t e = gpio_isr_handler_add(gpio,Button::intr,this))
		log_error(TAG,"register isr: %s",esp_err_to_name(e));
}


// this is an ISR!
void Button::intr(void *arg)
{
	// no log_* from ISRs!
	int32_t now = esp_timer_get_time() / 1000;
	Button *b = static_cast<Button*>(arg);
	event_t ev, xev = 0;
	if (gpio_get_level(b->m_gpio) == b->m_presslvl) {
		b->m_tpressed = now;
		ev = b->m_pev;
	} else {
		ev = b->m_rev;
		unsigned dt = now - b->m_tpressed;
		if (dt < BUTTON_SHORT_START) {
			// fast debounce
		} else if (dt < BUTTON_SHORT_END) {
			xev = b->m_sev;
		} else if ((dt >= BUTTON_MED_START) && (dt < BUTTON_MED_END)) {
			xev = b->m_mev;
		} else if ((dt >= BUTTON_LONG_START) && (dt < BUTTON_LONG_END)) {
			xev = b->m_lev;
		}
	}
	event_isr_trigger(ev);
	if (xev)
		event_isr_trigger(xev);
}

#endif
