/*
 *  Copyright (C) 2022-2025, Thomas Maier-Komor
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

#ifndef ROTARYENCODER_H
#define ROTARYENCODER_H

#include <event.h>
#include <xio.h>


class RotaryEncoder
{
	public:
	static RotaryEncoder *create(const char *name, xio_t clk, xio_t dt, xio_t sw = XIO_INVALID, xio_cfg_pull_t = xio_cfg_pull_none);

	event_t pressed_event() const
	{ return m_pev; }

	event_t released_event() const
	{ return m_rev; }

	event_t rotleft_event() const
	{ return m_rlev; }

	event_t rotright_event() const
	{ return m_rrev; }

	private:
	RotaryEncoder(const char *name, xio_t clk, xio_t dt, xio_t sw = XIO_INVALID);

	static void sw_ev(void *arg);
	static void clk_ev(void *arg);
	static void swIntr(void *arg);
	static void clkIntr(void *arg);
	static void dtIntr(void *arg);

	uint32_t m_ptime = 0;
	xio_t m_clk, m_dt, m_sw;
	uint8_t m_lc = 0, m_lst = 0, m_lsw = 1;
	bool m_le = false;
	event_t m_rev, m_pev, m_sev, m_mev, m_rlev, m_rrev;
};

#endif
