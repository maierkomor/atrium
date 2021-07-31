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
#include <stdlib.h>
#include <esp_timer.h>

#ifdef CONFIG_FUNCTION_TIMING

#if 1
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
#ifdef CONFIG_IDF_TARGET_ESP8266
	typedef uint32_t hrtime_t;
#else
	typedef std::chrono::time_point<std::chrono::high_resolution_clock> hrtime_t;
#endif

	FProf(TimeStats &s)
	: stats(s)
	{
		if (0 == s.level++) {
#ifdef CONFIG_IDF_TARGET_ESP8266
			start = esp_timer_get_time();
//			asm volatile ("rsr %0, ccount" : "=r"(start));
#else
			start = std::chrono::high_resolution_clock::now();
#endif
		}
	}

	~FProf()
	{
		if (0 == --stats.level) {
#ifdef CONFIG_IDF_TARGET_ESP8266
			uint32_t end = esp_timer_get_time();
//			uint32_t end;
//			asm volatile ("rsr %0, ccount" : "=r"(end));
			uint32_t dt = end-start;
#else
			hrtime_t end = std::chrono::high_resolution_clock::now();
			uint32_t dt = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
#endif
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
#define PROFILE_FUNCTION() static thread_local TimeStats _stats(__FUNCTION__); FProf _prof(_stats)
#define PROFILE_BLOCK() static thread_local TimeStats _stats(__FILE__ ":" TOSTRING(__LINE__)); FProf _prof(_stats)


#else
#include "log.h"
#include <esp_timer.h>
#include <map>

struct TimeStats
{
	uint32_t low = 0, high = 0, total = 0, count = 0;

	TimeStats()
	{ }

	explicit TimeStats(uint32_t dt)
	: low(dt)
	, high(dt)
	, total(dt)
	, count(1)
	{ }
};

class TimeDelta
{
	public:
	explicit TimeDelta(const char *n)
	: m_start(esp_timer_get_time())
	, m_name(n)
	{ }

	~TimeDelta();

	static TimeStats getStats(const char *);
	static const std::map<const char *,TimeStats> &getStats()
	{ return Stats; }

	private:
	TimeDelta(const TimeDelta &);
	TimeDelta& operator = (const TimeDelta &);

	int64_t m_start;
	const char *m_name;
	static std::map<const char *,TimeStats> Stats;
};

#define PROFILE_FUNCTION() TimeDelta dt(__FUNCTION__);

#endif

#else	// disabled

#define PROFILE_FUNCTION()

#endif

#endif
