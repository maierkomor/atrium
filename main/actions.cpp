/*
 *  Copyright (C) 2017-2020, Thomas Maier-Komor
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

#include <sdkconfig.h>

#include "actions.h"
#include "cyclic.h"
#include "log.h"
#include "globals.h"
#include "settings.h"
#include "strstream.h"
#include "termstream.h"
#include "terminal.h"

#include <assert.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#ifdef ESP32
#include <rom/gpio.h>
#endif

#include <vector>

using namespace std;


vector<Action> Actions;

static char TAG[] = "action";

#ifdef CONFIG_AT_ACTIONS
const char *Weekdays_en[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa", "wd", "we", "ed", "hd" };
const char *Weekdays_de[] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa", "wt", "we", "jt", "ft" };
#endif


static Action *get_action(const char *name)
{
	assert(name);
	for (size_t i = 0; i < Actions.size(); ++i) {
		assert(Actions[i].name);
		if (0 == strcasecmp(Actions[i].name,name))
			return &Actions[i];
		if ((Actions[i].text) && (0 == strcasecmp(Actions[i].text,name)))
			return &Actions[i];
	}
	return 0;
}


void add_action(const char *name, void (*func)(), const char *text)
{
	Action a;
	a.name = name;
	a.func = func;
	a.text = text;
	Actions.push_back(a);
	log_dbug(TAG,"added action %s",name);
}


int action(Terminal &t, int argc, const char *args[])
{
	if (argc != 2) {
		t.printf("%s: 1 argument expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (0 == strcmp(args[1],"-l")) {
		for (size_t i = 0, e = Actions.size(); i != e; ++i)
			t.printf("\t%-15s  %s\n",Actions[i].name,Actions[i].text);
		return 0;
	}
	Action *a = get_action(args[1]);
	if (a == 0) {
		t.printf("unable to find action %s\n",args[1]);
		return 1;
	}
	a->func();
	return 0;
}


#ifdef CONFIG_AT_ACTIONS
static const char *get_action_text(const char *name)
{
	for (size_t i = 0; i < Actions.size(); ++i) {
		if (0 == strcasecmp(Actions[i].name,name)) {
			if (Actions[i].text)
				return Actions[i].text;
			return name;
		}
	}
	return name;
}


#ifdef CONFIG_HOLIDAYS
static bool is_holiday(uint8_t d, uint8_t m, unsigned y)
{
	for (const Date &hd : Config.holidays()) {
		unsigned sd = hd.day();
		unsigned sm = hd.month();
		if (hd.has_endday()) {
			unsigned sy = hd.year();
			unsigned ey = sy + hd.endyear();
			unsigned doy = d + m*31 + y * 31 * 12;
			unsigned em = hd.endmonth();
			unsigned ed = hd.endday();
			unsigned so = sd + sm*31 + sy * 31 * 12;
			unsigned eo = ed + em*31 + ey * 31 * 12;
			log_info(TAG,"doy = %u, so = %u, eo = %u",doy,so,eo);
			if ((doy >= so) && (doy <= eo))
				return true;
		} else if ((d == sd) && (m == sm) 
			&& (!hd.has_year() || (y == hd.year()))) {
			log_info(TAG,"direct match d=%u, m=%u, y=%u, sd=%u, sm=%u",d,m,y,sd,sm);
			return true;
		}
	}
	return false;
}
#endif


static unsigned actions_loop()
{
	static uint8_t last_m = 0xf0;
	uint8_t h,m,s,d,md,mon;
	unsigned y;
	get_time_of_day(&h,&m,&s,&d,&md,&mon,&y);
	if (h > 23)
		return 300;
	if (m == last_m)
		return 300;
	last_m = m;
	char now[32];
	sprintf(now,"%s, %u:%02u",Weekdays_de[d],h,m);
	RTData.set_ltime(now);
	if (!Config.actions_enable())
		return 2000;
	//dbug("alarmclock() check %s %u:%02u",WeekDay_str((WeekDay)d),h,m);
	vector<AtAction> &atactions = *Config.mutable_at_actions();
	for (size_t i = 0; i < atactions.size(); ++i) {
		AtAction &a = atactions[i];
		if (!a.enable())
			continue;
		if (((a.min_of_day()/60) != h) || ((a.min_of_day()%60) != m))
			continue;
		bool x = false;
		WeekDay wd = a.day();
#ifdef CONFIG_HOLIDAYS
		if (is_holiday(md,mon,y))
			x = (wd == Holiday);
		else
#endif
		if ((wd == (WeekDay)d) || (wd == EveryDay)
			|| ((wd == WorkDay) && ((d > 0) && (d < 6)))
			|| ((wd == WeekEnd) && ((d == 0) || (d == 6)))) {
			x = true;
		}
		if (x) {
			const char *aname = a.action().c_str();
			if (Action *x = get_action(aname)) {
				log_info(TAG,"check_alarm() triggering %s\n",aname);
				x->func();
			} else {
				log_warn(TAG,"check_alarm() unable to execute unknown action '%s'\n",aname);
			}
		}
	}
	return 300;
}


extern "C"
void actions_setup()
{
	if (Config.actions_enable())
		RTData.set_timers_enabled(true);
	add_cyclic_task("actions",actions_loop);
}


#ifdef CONFIG_HOLIDAYS
int holiday(Terminal &t, int argc, const char *args[])
{
	if (argc > 3) {
		t.printf("%s: 0-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if ((argc == 1) || ((argc == 2) && (0 == strcmp(args[1],"-h")))) {
		t.printf("-l to list all holidays\n"
			"-j to print json output\n"
			"-D {<id>|all} to delete all holidays or holiday with id <id>\n"
			"<dd.mm.yyyy|mm/dd/yyyy|yyyy-mm-dd>> to add a holiday for the specified date\n");
		return 0;
	}
	if (!strcmp(args[1],"-D") && (argc == 3)) {
		long l = strtol(args[2],0,10);
		if ((l < 0) || (l >= (long)Config.holidays_size())) {
			t.printf("value out of range\n");
			return 1;
		}
		if (!strcmp(args[2],"all"))
			Config.clear_holidays();
		else
			Config.mutable_holidays()->erase(Config.mutable_holidays()->begin()+l);
		return 0;
	}
	// 2 arguments
	if (!strcmp(args[1],"-l")) {
		for (size_t i = 0; i < Config.holidays_size(); ++i) {
			const Date &h = Config.holidays(i);
			if (h.has_endday())
				t.printf("%d: %d.%d.%d-%d.%d.%d\n",i,h.day(),h.month(),h.year(),h.endday(),h.endmonth(),h.year()+h.endyear());
			else if (h.has_year())
				t.printf("%d: %d.%d.%d\n",i,h.day(),h.month(),h.year());
			else
				t.printf("%d: %d.%d\n",i,h.day(),h.month());
		}
		uint8_t hr,m,s,d,md,mon;
		unsigned y;
		get_time_of_day(&hr,&m,&s,&d,&md,&mon,&y);
		t.printf("today is %sa holiday\n", is_holiday(md,mon,y) ? "" : "not ");
		return 0;
	}
	if (!strcmp(args[1],"-j")) {
		t.printf("{\"holidays\":[");
		TermStream ts(t);
		for (size_t i = 0; i < Config.holidays_size(); ++i) {
			Config.holidays(i).toJSON(ts);
			if (i + 1 != Config.holidays_size())
				ts << ',';
		}
		t.print("]}\n");
		return 0;
	}
	int y,m,d;
	int n = sscanf(args[1],"%d.%d.%d",&d,&m,&y);
	if (n < 2) {
		n = sscanf(args[1],"%d/%d/%d",&m,&d,&y);
		if (n < 2) {
			n = sscanf(args[1],"%d-%d-%d",&y,&m,&d);
			if (n < 2) {
				t.printf("unrecognized input format\n");
				return 1;
			}
		}
	}
	if ((m <= 0) || (m > 12)) {
		t.printf("month out of range\n");
		return 1;
	}
	if ((d <= 0) || (d > 31)) {
		t.printf("day out of range\n");
		return 1;
	}
	Date h;
	h.set_month(m);
	h.set_day(d);
	if ((n == 3) && (y != 0))
		h.set_year(y);
	if (argc == 3) {
		n = sscanf(args[2],"%d.%d.%d",&d,&m,&y);
		if (n < 3) {
			n = sscanf(args[2],"%d/%d/%d",&m,&d,&y);
			if (n < 3) {
				n = sscanf(args[2],"%d-%d-%d",&y,&m,&d);
				if (n < 2) {
					t.printf("invalid input format\n");
					return 1;
				}
			}
		}
		if ((m <= 0) || (m > 12)) {
			t.printf("invalid month\n");
			return 1;
		}
		if ((d <= 0) || (d > 31)) {
			t.printf("invalid day\n");
			return 1;
		}
		h.set_endday(d);
		h.set_endmonth(m);
		h.set_endyear(y-h.year());
	}
	*Config.add_holidays() = h;
	return 0;
}
#endif


static void printHelp(Terminal &t)
{
	t.printf("synopsys:\n"
		"at -l: to list timers\n"
		"at -1: to enable all alarms\n"
		"at -0: to disable all alarms\n"
		"at -e [all|<id>]: to enable alarm <id>\n"
		"at -d [all|<id>]: to disable alarm <id>\n"
		"at -D <id>: to delete alarm <id>\n"
		"at -a: to list available actions\n"
		"at -s: to save alarm settings\n"
		"at {weekday} {hour}:{minute} {action}\n"
		"   where weekday is one of: mon, tue, wed, thu, fri, sat, sun,\n"
		"   wd for workday,\n"
		"   we for weekend,\n"
		"   hd for holiday,\n"
		"   ed for every day.\n"
		"at processing is %s, config is %s\n"
		, Config.actions_enable() ? "enabled" : "disable"
		, Config.has_actions_enable() ? "set" : "unavailable");
}


int at(Terminal &t, int argc, const char *args[])
{
	if (argc > 4) {
		t.printf("%s: 0-3 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		printHelp(t);
		return 0;
	}
	if (argc == 2) {
		if (!strcmp(args[1],"-l")) {
			for (size_t i = 0; i < Config.at_actions_size(); ++i) {
				const AtAction &a = Config.at_actions(i);
				t.printf("\t[%d]: %s %u:%02u %s%s\n",i,WeekDay_str(a.day()),a.min_of_day()/60,a.min_of_day()%60,get_action_text(a.action().c_str()),a.enable()?"":" (disabled)");
			}
		} else if (!strcmp(args[1],"-0")) {
			Config.set_actions_enable(false);
			RTData.set_timers_enabled(false);
			t.printf("timers disabled\n");
		} else if (!strcmp(args[1],"-1")) {
			Config.set_actions_enable(true);
			RTData.set_timers_enabled(true);
			t.printf("timers enabled\n");
		} else if (!strcmp(args[1],"-a")) {
			for (size_t i = 0; i < Actions.size(); ++i)
				t.printf("\t%s ('%s')\n",Actions[i].name,Actions[i].text ? Actions[i].text : "");
		} else if (!strcmp(args[1],"-h")) {
			printHelp(t);
		} else if (!strcmp(args[1],"-s")) {
			storeSettings();
		} else if (!strcmp(args[1],"-j")) {
			t.printf("{\"alarms\":[");
			TermStream ts(t);
			for (size_t i = 0; i < Config.at_actions_size(); ++i) {
				Config.at_actions(i).toJSON(ts);
				if (i + 1 != Config.at_actions_size())
					ts << ',';
			}
			t.print("]}\n");
		} else {
			t.printf("invalid argument\n");
			return 1;
		}
		return 0;
	}
	if (argc == 3) {
		bool all = !strcmp(args[2],"all");
		long id = strtol(args[2],0,10);
		if ((id == 0) && ((args[2][0] < '0') || (args[2][0] > '9')))
			id = all ? 0 : -1;
		if ((id >= (long)Config.at_actions_size()) || (id < 0)) {
			t.printf("invalid id\n");
			return 1;
		}
		if (!strcmp(args[1],"-e") || !strcmp(args[1],"-d")) {
			bool enable = args[1][1] == 'e';
			if (!all) {
				Config.mutable_at_actions(id)->set_enable(enable);
				return 0;
			}
			for (size_t i = 0; i < Config.at_actions_size(); ++i)
				Config.mutable_at_actions(id)->set_enable(enable);
			return 0;
		}
		if (!strcmp(args[1],"-D")) {
			if (all)
				Config.clear_at_actions();
			else
				Config.mutable_at_actions()->erase(Config.mutable_at_actions()->begin()+id);
			return 0;
		}
		t.printf("invalid argument\n");
		return 1;
	}
	bool have_day = false;
	WeekDay wd;
	for (size_t i = 0; i < sizeof(Weekdays_de)/sizeof(Weekdays_de[0]); ++i) {
		if (0 == strcasecmp(Weekdays_de[i],args[1])) {
			wd = (WeekDay)i;
			have_day = true;
			break;
		}
	}
	if (!have_day) {
		for (size_t i = 0; i < sizeof(Weekdays_en)/sizeof(Weekdays_en[0]); ++i) {
			if (0 == strcasecmp(Weekdays_en[i],args[1])) {
				wd = (WeekDay)i;
				have_day = true;
				break;
			}
		}
		if (!have_day) {
			t.printf("invalid day spec\n");
			return 1;
		}
	}
	int h,m;
	int n = sscanf(args[2],"%d:%d",&h,&m);
	if ((2 != n) || (h < 0) || (h > 23) || (m < 0) || (m > 59)) {
		t.printf("invalid time spec (n=%d, h=%d, m=%d)\n",n,h,m);
		return 1;
	}
	Action *x = get_action(args[3]);
	if ((x == 0) || (x->func == 0)) {
		t.printf("unknown action %s\n",args[3]);
		return 1;
	}
	AtAction *a = Config.add_at_actions();
	a->set_min_of_day(h*60+m);
	a->set_action(args[3]);
	a->set_day(wd);
	a->set_enable(true);
	return 0;
}

#endif // CONFIG_AT_ACTIONS
