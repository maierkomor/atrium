/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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

#ifndef BUZZER_DRV_H
#define BUZZER_DRV_H

#include "xio.h"

struct Buzzer
{
	explicit Buzzer(xio_t xio);

	private:
	static Buzzer *m_first;
	Buzzer *m_next = 0;
	const char *m_name;
	xio_t m_gpio;
	unsigned m_freq = 0, m_dur = 0;
};

#endif
