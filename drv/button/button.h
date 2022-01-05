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

#ifndef BUTTON_DRV_H
#define BUTTON_DRV_H

#include <event.h>
#include <driver/gpio.h>

#define BUTTON_SHORT_START	40
#define BUTTON_SHORT_END	350
#define BUTTON_MED_START	600
#define BUTTON_MED_END		1000
#define BUTTON_LONG_START	1500
#define BUTTON_LONG_END		3000



class Button
{
	public:
	Button()
	: m_gpio(GPIO_NUM_MAX)
	, m_rev(0)
	, m_pev(0)
	{ 

	}

	Button(const char *name, gpio_num_t gpio, gpio_pull_mode_t mode, bool active_high, Button *n);

	void init(const char *, gpio_num_t gpio, gpio_pull_mode_t mode, bool active_high);

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

	Button *getNext() const
	{ return m_next; }

	void setNext(Button *n)
	{ m_next = n; }

	private:
	static void intr(void *);
	static unsigned cyclic(void *);

	Button *m_next = 0;
	const char *m_name;
	int32_t m_tpressed;
	gpio_num_t m_gpio;
	uint8_t m_presslvl;
	event_t m_rev, m_pev, m_sev, m_mev, m_lev;
};


#endif
