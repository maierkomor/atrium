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

#ifndef BUTTON_DRV_H
#define BUTTON_DRV_H

#include <event.h>
#include "xio.h"

#define BUTTON_SHORT_START	40
#define BUTTON_SHORT_END	350
#define BUTTON_MED_START	600
#define BUTTON_MED_END		1000
#define BUTTON_LONG_START	1500
#define BUTTON_LONG_END		3000


class Button
{
	public:
	static Button *create(const char *name, xio_t gpio, xio_cfg_pull_t mode, bool active_high);

	int32_t pressed_at() const
	{ return m_tpressed; }

	event_t pressed_event() const
	{ return m_pev; }

	event_t released_event() const
	{ return m_rev; }

	event_t press_short_event() const
	{ return m_sev; }

	event_t press_medium_event() const
	{ return m_mev; }

	event_t press_long_event() const
	{ return m_lev; }

	const char *name() const
	{ return m_name; }

	private:
	Button(const char *name, xio_t gpio, xio_cfg_pull_t mode, bool active_high);
	static void intr(void *);
	static void press_ev(void *);
	static void release_ev(void *);

	typedef enum btnst_e { btn_unknown = 0, btn_pressed, btn_released } btnst_t;

	const char *m_name;
	int32_t m_tpressed;
	xio_t m_gpio;
	int8_t m_presslvl;
	btnst_t m_st = btn_unknown;
	event_t m_rev, m_pev, m_sev, m_mev, m_lev, m_lastev = 0;
};


#endif
