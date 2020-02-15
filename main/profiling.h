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

#ifndef PROFILING_H
#define PROFILING_H

#include "log.h"
#include <esp_timer.h>

class TimeDelta
{
	public:
	explicit TimeDelta(const char *m = 0)
	: m_start(esp_timer_get_time())
	, m_msg(m)
	{ }

	~TimeDelta()
	{
		int64_t dt = esp_timer_get_time() - m_start;
		if (m_msg)
			con_printf("%s: %ld\n",m_msg,dt);
		else
			con_printf("%ld\n",m_msg,dt);
	}

	private:
	TimeDelta(const TimeDelta &);
	TimeDelta& operator = (const TimeDelta &);

	int64_t m_start;
	const char *m_msg;
};

#endif
