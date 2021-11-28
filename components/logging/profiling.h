/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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

#include <sdkconfig.h>

#ifdef CONFIG_FUNCTION_TIMING

#include <stdlib.h>
#include <esp_timer.h>
#include <chrono>

struct TimeStats
{
	const char *name;
	TimeStats *next;
	uint64_t total = 0;
	uint32_t low = UINT32_MAX, high = 0;
	uint_least16_t count = 0, level = 0;

	static TimeStats *First;

	TimeStats(const char *n)
	: name(n)
	, next(First)
	{
		First = this;
	}

	~TimeStats()
	{
		abort();
	}
};


struct FProf
{
	typedef int64_t hrtime_t;

	FProf(TimeStats &s)
	: stats(s)
	{
		if (0 == s.level++) {
			start = esp_timer_get_time();
		}
	}

	~FProf()
	{
		if (0 == --stats.level) {
			hrtime_t end = esp_timer_get_time();
			hrtime_t dt = end-start;
			stats.total += dt;
			if (dt < stats.low)
				stats.low = dt;
			if (dt > stats.high)
				stats.high = dt;
			++stats.count;
		}
	}

	TimeStats &stats;
	hrtime_t start = 0;
};

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define PROFILE_FUNCTION() static TimeStats _stats(__FUNCTION__); FProf _prof(_stats)
#define PROFILE_BLOCK() static thread_local TimeStats _stats(__FILE__ ":" TOSTRING(__LINE__)); FProf _prof(_stats)


#else	// disabled

#define PROFILE_FUNCTION()

#endif

#endif
