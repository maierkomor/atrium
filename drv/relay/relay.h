/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

#ifndef RELAY_DRV_H
#define RELAY_DRV_H

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include <driver/gpio.h>

#include <event.h>

class Relay
{
	public:
	Relay(const char *, gpio_num_t gpio, uint16_t config, uint32_t minitv);

	void set(bool);
	void turn_on();
	void turn_off();
	void toggle();
	uint32_t time_on() const;

	bool is_on() const
	{ return m_state; }

	const char *name() const
	{ return m_name; }

	gpio_num_t gpio() const
	{ return m_gpio; }

	uint16_t config() const
	{ return m_config; }

	static Relay *first()
	{ return Relays; }

	Relay *next() const
	{ return m_next; }

	static Relay *get(const char *);

	void setCallback(void (*cb)(Relay *))
	{ m_cb = cb; }

	Relay *getInterlock() const
	{ return m_interlock; }

	void setInterlock(Relay *r)
	{ m_interlock = r; }

	private:
	static void timerCallback(void *);
	void sync();

	static Relay *Relays;
	Relay *m_next = 0, *m_interlock = 0;
	const char *m_name;
	TimerHandle_t m_tmr;
	uint32_t m_tlt = 0		// time of last toggle
		, m_minitv = 0;		// minimum toggle interval
	gpio_num_t m_gpio;
	event_t m_onev = 0, m_offev = 0, m_changedev = 0;
	uint16_t m_config;
	bool m_state = false, m_set = false;
	void (*m_cb)(Relay *);
};

#endif
