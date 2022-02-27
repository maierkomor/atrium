/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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
#include "event.h"
#include "log.h"

#include <esp_system.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG MODULE_BUTTON


Button::Button(const char *name, xio_t gpio, xio_cfg_pull_t mode, bool active_high)
: m_name(name)
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


Button *Button::create(const char *name, xio_t gpio, xio_cfg_pull_t mode, bool active_high)
{
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = mode;
	cfg.cfg_intr = xio_cfg_intr_edges;
	if (0 > xio_config(gpio,cfg)) {
		log_warn(TAG,"config gpio %u failed",gpio);
	} else {
		Button *b = new Button(name,gpio,mode,active_high);
		event_t fev = xio_get_fallev(gpio);
		event_t rev = xio_get_riseev(gpio);
		if (fev && rev) {
			Action *fa = action_add(concat(name,"!down"),press_ev,b,0);
			event_callback(fev,fa);
			Action *ra = action_add(concat(name,"!up"),release_ev,b,0);
			event_callback(rev,ra);
			log_dbug(TAG,"listen on events");
		} else if (0 == xio_set_intr(gpio,intr,b)) {
			log_dbug(TAG,"listen on interrupt");
		}
		return b;
	}
	return 0;
}


void Button::press_ev(void *arg)
{
	int32_t now = esp_timer_get_time() / 1000;
	Button *b = static_cast<Button*>(arg);
	log_dbug(TAG,"%s pressed",b->m_name);
	b->m_tpressed = now;
	if (b->m_st != btn_released) {
		b->m_st = btn_released;
		event_trigger(b->m_pev);
	}
}


void Button::release_ev(void *arg)
{
	int32_t now = esp_timer_get_time() / 1000;
	Button *b = static_cast<Button*>(arg);
	log_dbug(TAG,"%s released",b->m_name);
	if (b->m_st != btn_released) {
		event_t ev = 0;
		unsigned dt = now - b->m_tpressed;
		b->m_st = btn_released;
		event_trigger(b->m_rev);
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
	event_t ev, xev = 0;
	if (xio_get_lvl(b->m_gpio) == b->m_presslvl) {
		b->m_tpressed = now;
		ev = b->m_pev;
	} else {
		ev = b->m_rev;
		unsigned dt = now - b->m_tpressed;
		if (dt < BUTTON_SHORT_START) {
			// fast debounce
			return;
		}
		b->m_tpressed = 0;
		if (dt < BUTTON_SHORT_END) {
			xev = b->m_sev;
		} else if ((dt >= BUTTON_MED_START) && (dt < BUTTON_MED_END)) {
			xev = b->m_mev;
		} else if ((dt >= BUTTON_LONG_START) && (dt < BUTTON_LONG_END)) {
			xev = b->m_lev;
		}
	}
	if (ev == b->m_lastev)
		return;
	b->m_lastev = ev;
	event_isr_trigger(ev);
	if (xev)
		event_isr_trigger(xev);
}

#endif
