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


#include "globals.h"
#include "hwcfg.h"
#include "ujson.h"
#include "log.h"
#include "profiling.h"
#include "swcfg.h"

#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

using namespace std;

const char ResetReasons[][12] = {
	"unknown",
	"powerup",
	"external",
	"software",
	"panic",
	"internal_wd",
	"task_wd",
	"watchdog",
	"deepsleep",
	"brownout",
	"sdio",
};

NodeConfig Config;
HardwareConfig HWConf;
nvs_handle NVS = 0;

JsonObject *RTData = 0;
JsonNumber *Uptime = 0;
#ifdef CONFIG_OTA
JsonString *UpdateState = 0;
#endif


static const char TAG[] = "time";

static SemaphoreHandle_t Mtx = 0;


/*
static JsonObject *rtd_get_obj(const char *name)
{
	JsonElement *e = RTData->get(name);
	assert(e);
	return e->to_object();
}


static JsonElement *rtd_get(const char *cat, const char *item)
{
	JsonObject *o;
	if (cat)
		o = rtd_get_obj(cat);
	else
		o = RTData;
	return o->getn(item);
}
*/

void globals_setup()
{
	HWConf.clear();	// initialize defaults
	Config.clear();
	RTData = new JsonObject(0);
	Mtx = xSemaphoreCreateMutex();
}

void rtd_lock()
{
	xSemaphoreTake(Mtx,portMAX_DELAY);
}


void rtd_unlock()
{
	xSemaphoreGive(Mtx);
}


void rtd_update()
{
	if (Uptime == 0)
		Uptime = RTData->add("uptime",0.0);
	Uptime->set(timestamp()/1000000ULL);
}


void runtimedata_to_json(stream &json)
{
	TimeDelta dt(__FUNCTION__);
	rtd_lock();
	rtd_update();
	RTData->toStream(json);
	rtd_unlock();
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


#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP8266
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


