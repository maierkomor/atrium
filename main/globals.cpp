/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#include "globals.h"
#include "binformats.h"
#include "log.h"
#include "strstream.h"

#include <esp_timer.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

using namespace std;

uint64_t Uptime = 0;	// in seconds
NodeConfig Config;
RunTimeData RTData;
bool DhtDebug = false;
bool DHTEnabled = false;
unsigned DHTInterval = 5000;
static char TAG[] = "time";


void runtimedata_to_json(strstream &json)
{
	static bool b = false;
	if (RTData.relay() != b) {
		b = RTData.relay();
		log_info("webdata","relay state change");
	}
	RTData.set_uptime(timestamp()/1000);
	RTData.toJSON(json);
}


static int daylight_saving_gmt(struct tm *tm)
{
	// input in GMT
	// tm_mday: [1..31]
	// tm_wday: [0..6]
	// tm_mon:  [0..11]
	// wintertime GMT+1
	// summertime GMT+2
	// last sunday of march at 02:00/CET (1:00/GMT): +1h, to +2
	// last sunday of october at 03:00/CEST (1:00/GMT): -1h, to +1
	if ((tm->tm_mon < 2) || (tm->tm_mon > 9))	// jan,feb,nov,dec are wintertime
		return 1;
	if ((tm->tm_mon > 2) && (tm->tm_mon < 9))	// apr,may,jun,jul,aug,sep are summertine
		return 2;
	if (tm->tm_mon == 2) {
		if ((tm->tm_mday-tm->tm_wday) < 25)			// Sun, 25th is earlies possible switching date
			return 1;	// still winter
		if ((tm->tm_wday == 0) && (tm->tm_hour == 0))		// last hour before switching
			return 1;
		return 2;
	} else if (tm->tm_mon == 9) {
		if ((tm->tm_mday-tm->tm_wday) < 25)			// Sun, 25th is earlies possible switching date
			return 2;	// still summer
		if ((tm->tm_wday == 0) && (tm->tm_hour == 0))		// last hour before switching
			return 2;
		return 1;
	}
	log_error(TAG,"daylight_saving_gmt: BUG");
	return 0;
}


static int daylight_saving_cet(struct tm *tm)
{
	// input in CET
	// tm_mday: [1..31]
	// tm_wday: [0..6]
	// tm_mon:  [0..11]
	// wintertime GMT+1
	// summertime GMT+2
	// last sunday of march at 02:00/CET (1:00/GMT): +1h, to +2
	// last sunday of october at 03:00/CEST (1:00/GMT): -1h, to +1
	if ((tm->tm_mon < 2) || (tm->tm_mon > 9))	// jan,feb,nov,dec are wintertime
		return 1;
	if ((tm->tm_mon > 2) && (tm->tm_mon < 9))	// apr,may,jun,jul,aug,sep are summertine
		return 2;
	if (tm->tm_mon == 2) {
		if ((tm->tm_mday-tm->tm_wday) < 25)			// Sun, 25th is earlies possible switching date
			return 1;	// still winter
		if ((tm->tm_wday == 0) && (tm->tm_hour <= 1))		// last hour before switching
			return 1;
		return 2;
	} else if (tm->tm_mon == 9) {
		if ((tm->tm_mday-tm->tm_wday) < 25)			// Sun, 25th is earlies possible switching date
			return 2;	// still summer
		if ((tm->tm_wday == 0) && (tm->tm_hour <= 2))		// last hour before switching
			return 2;
		return 1;
	}
	log_error(TAG,"daylight_saving_cet: BUG");
	return 0;
}


#if defined ESP32 || defined ESP8266
void get_time_of_day(uint8_t *h, uint8_t *m, uint8_t *s, uint8_t *wd, uint8_t *mday, uint8_t *month, unsigned *year)
{
	time_t now;
	time(&now);
	if (now == 0) {
		*h = 0xff;
		*m = 0xff;
		return;
	}
	struct tm tm;
	gmtime_r(&now,&tm);
	//dbug("get_time_of_day(): %u:%02u:%02u, %s %d.%d, TZ%d",tm.tm_hour,tm.tm_min,tm.tm_sec,Weekdays_en[tm.tm_wday],tm.tm_mday,tm.tm_mon+1,Timezone);
	int tz = 0;
	if (Config.has_timezone()) {
		const char *t = Config.timezone().c_str();
		if ((t[0] >= '0' && t[0] <= '9')) {
			tz = strtol(t,0,0);
		} else if (!strcmp(t,"CET")) {
			tz = daylight_saving_cet(&tm);
		}
		if (tz == 0)
			tz = daylight_saving_gmt(&tm);
		if (tz != 0) {
			tm.tm_hour += tz;
			if (tm.tm_hour < 0)
				tm.tm_hour += 24;
			else if (tm.tm_hour > 23)
				tm.tm_hour -= 24;
		}
	}
	if (h)
		*h = tm.tm_hour;
	if (m)
		*m = tm.tm_min;
	if (s)
		*s = tm.tm_sec;
	if (wd)
		*wd = tm.tm_wday;
	if (month)
		*month = tm.tm_mon + 1;
	if (mday)
		*mday = tm.tm_mday;
	if (year)
		*year = 1900 + tm.tm_year;
}

#else
// can be used when the platform supports it
void get_time_of_day(uint8_t *h, uint8_t *m, uint8_t *s, uint8_t *wd, uint8_t *md, uint8_t *mon, unsigned *year)
{
	time_t now;
	time(&now);
	struct tm tm;
	localtime_r(&now,&tm);
	*h = tm.tm_hour;
	*m = tm.tm_min;
	if (s)
		*s = tm.tm_sec;
	if (wd)
		*wd = tm.tm_wday;
	if (md)
		*md = tm.tm_mday;
	if (mon)
		*mon = tm.tm_mon;
	if (year)
		*year = tm.tm_year;
}
#endif


