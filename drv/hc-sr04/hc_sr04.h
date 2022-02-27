/*
 *  Copyright (C) 2020-2022, Thomas Maier-Komor
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

#ifndef HC_SR04_H
#define HC_SR04_H

#include <stdint.h>
#include "event.h"
#include "xio.h"


class HC_SR04
{
	public:
	static HC_SR04 *create(int8_t trigger, int8_t echo); 
	int attach(class EnvObject *o);
	void trigger();
	void setName(const char *name);

	const char *getName() const
	{ return m_name; }

	static HC_SR04 *getFirst()
	{ return First; }

	HC_SR04 *getNext()
	{ return m_next; }

	private:
	HC_SR04(xio_t trigger, xio_t echo);

	static void hc_sr04_isr(void *);
	static void update(void *);

	static HC_SR04 *First;

	int64_t m_start = 0;
	int32_t m_dt = 0;
	HC_SR04 *m_next = 0;
	class EnvNumber *m_dist = 0;
	char *m_name = 0;
	xio_t m_trigger, m_echo;
	event_t m_ev = 0;
};


#endif
