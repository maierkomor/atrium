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

#include "profiling.h"

#ifdef CONFIG_FUNCTION_TIMING

#include "terminal.h"

using namespace std;

#if 1

TimeStats *TimeStats::First = 0;

int prof(Terminal &term, int argc, const char *args[])
{
	term.printf("%8s %9s %10s %8s %5s %s\n","low","avg","high","total","calls","function");
	TimeStats *s = TimeStats::First;
	while (s) {
		if (s->count)
			term.printf("%8lu %9lu %10lu %8lu %5lu %s\n",s->low,(long unsigned)(s->total/s->count),(long unsigned)s->high,(long unsigned)s->total,(long unsigned)s->count,s->name);
		s = s->next;
	}
	return 0;
}


#else
map<const char *,TimeStats> TimeDelta::Stats;


TimeDelta::~TimeDelta()
{
	int64_t dt = esp_timer_get_time() - m_start;
	auto i = Stats.find(m_name);
	if (i != Stats.end()) {
		i->second.total += dt;
		if (dt < i->second.low)
			i->second.low = dt;
		if (dt > i->second.high)
			i->second.high = dt;
		i->second.total += dt;
		++i->second.count;
	} else {
		Stats.emplace(m_name,TimeStats(dt));
	}
}

TimeStats TimeDelta::getStats(const char *n)
{
	auto i = Stats.find(n);
	if (i != Stats.end())
		return TimeStats{};
	return i->second;
}

int prof(Terminal &term, int argc, const char *args[])
{
	term.printf("%5s %6s %8s %5s %s\n","low","avg","high","calls","function");
	for (auto x : TimeDelta::getStats())
		term.printf("%5u %6u %8u %5u %s\n",x.second.low,x.second.total/x.second.count,x.second.high,x.second.count,x.first);
	return 0;
}
#endif
#endif
