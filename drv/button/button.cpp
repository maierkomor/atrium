/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#include "actions.h"
#include "button.h"
#include "env.h"
#include "event.h"
#include "log.h"

#include <esp_system.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <driver/gpio.h>

#define TAG MODULE_BUTTON


Button::Button(const char *name, xio_t gpio, xio_cfg_pull_t mode, bool active_high)
: m_name(name)
, m_pressed("pressed",false)
, m_ptime("ptime",0.0,"ms")
, m_gpio(gpio)
, m_presslvl((int8_t)active_high)
, m_rev(event_register(name,"`released"))
, m_pev(event_register(name,"`pressed"))
, m_sev(event_register(name,"`short"))
, m_mev(event_register(name,"`med"))
, m_lev(event_register(name,"`long"))
{ 
	log_info(TAG,"button %s at gpio %u",name,gpio);
}


void Button::attach(EnvObject *root)
{
	EnvObject *o = root->add(m_name);
	o->add(&m_pressed);
	o->add(&m_ptime);

}


Button *Button::create(const char *name, xio_t gpio, xio_cfg_pull_t mode, bool active_high)
{
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = mode;
	cfg.cfg_intr = xio_cfg_intr_edges;
	if (0 > xio_config(gpio,cfg)) {
		log_warn(TAG,"config gpio %u failed",gpio);
		return 0;
	} else {
		Button *b = new Button(name,gpio,mode,active_high);
		event_t fev = xio_get_fallev(gpio);
		event_t rev = xio_get_riseev(gpio);
		if ((rev == 0) || (fev == 0)) {
			if (0 == xio_set_intr(gpio,intr,b)) {
				b->m_intr = true;
				fev = b->m_pev;
				rev = b->m_rev;
				log_dbug(TAG,"use interrupts");
			} else {
				return 0;
			}
		}
		Action *fa = action_add(concat(name,"!down"),press_ev,b,0);
		event_callback(fev,fa);
		Action *ra = action_add(concat(name,"!up"),release_ev,b,0);
		event_callback(rev,ra);
		return b;
	}
	return 0;
}


void Button::press_ev(void *arg)
{
	Button *b = static_cast<Button*>(arg);
	if (!b->m_intr) {
		b->m_tpressed = esp_timer_get_time() / 1000;
		event_trigger(b->m_pev);
	}
	log_dbug(TAG,"%s pressed",b->m_name);
	if (b->m_st != btn_pressed) {
		b->m_st = btn_pressed;
		b->m_pressed.set(true);
		b->m_ptime.set(0);
	}
}


void Button::release_ev(void *arg)
{
	Button *b = static_cast<Button*>(arg);
	if (!b->m_intr) {
		b->m_treleased = esp_timer_get_time() / 1000;
		event_trigger(b->m_rev);
	}
	if (b->m_st != btn_released) {
		event_t ev = 0;
		unsigned dt = b->m_treleased - b->m_tpressed;
		log_dbug(TAG,"%s released %u",b->m_name,dt);
		b->m_st = btn_released;
		b->m_pressed.set(false);
		b->m_ptime.set((float)dt);
		if ((dt >= BUTTON_SHORT_START) && (dt < BUTTON_SHORT_END)) {
			ev = b->m_sev;
		} else if ((dt >= BUTTON_MED_START) && (dt < BUTTON_MED_END)) {
			ev = b->m_mev;
		} else if ((dt >= BUTTON_LONG_START) && (dt < BUTTON_LONG_END)) {
			ev = b->m_lev;
		} else
			return;
		event_trigger(ev);
	}
}


// this is an ISR!
void IRAM_ATTR Button::intr(void *arg)
{
	// no log_* from ISRs!
	int32_t now = esp_timer_get_time() / 1000;
	Button *b = static_cast<Button*>(arg);
	event_t ev = 0;
	if (xio_get_lvl(b->m_gpio) == b->m_presslvl) {
		if (b->m_lastev != b->m_pev) {
			ev = b->m_pev;
			b->m_tpressed = now;
		}
	} else {
		if (b->m_lastev != b->m_rev) {
			b->m_treleased = now;
			ev = b->m_rev;
		}
	}
	if (ev) {
		b->m_lastev = ev;
		event_isr_trigger(ev);
	}
}

#endif
