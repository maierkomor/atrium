/*
 *  Copyright (C) 2017-2023, Thomas Maier-Komor
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

#ifdef CONFIG_AT_ACTIONS
#include "actions.h"
#include "alarms.h"
#include "swcfg.h"
#include "cyclic.h"
#include "event.h"
#include "env.h"
#include "log.h"
#include "globals.h"
#include "settings.h"
#include "shell.h"
#include "terminal.h"

#include <assert.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>

#include <driver/gpio.h>

#include <vector>

// TODO: expand holiday concept to arbitrary categories, for adding
// "abscent" scenario.

using namespace std;

#define TAG MODULE_ALARMS
static EnvBool *Enabled = 0;

#ifdef CONFIG_HOLIDAYS
static bool is_holiday(uint8_t d, uint8_t m, unsigned y)
{
	unsigned doy = d | (m<<8) | (y <<16);
	log_dbug(TAG,"doy = %x",doy);
	for (const Date &hd : Config.holidays()) {
		unsigned sd = hd.day();
		unsigned sm = hd.month();
		if ((d == sd) && (m == sm) 
			&& (!hd.has_year() || (y == hd.year()))) {
//			log_dbug(TAG,"direct match d=%u, m=%u, y=%u, sd=%u, sm=%u",d,m,y,sd,sm);
			return true;
		}
		unsigned ed = hd.endday();
		unsigned em = hd.endmonth();
		if ((em != 0) && (ed != 0)) {
			unsigned sy = hd.year();
			unsigned ey = sy + hd.endyear();
			unsigned so = sd | (sm<<8) | (sy<<16);
			unsigned eo = ed | (em<<8) | (ey<<16);
			if ((doy >= so) && (doy <= eo)) {
//				log_dbug(TAG,"match so = %x, eo = %x",so,eo);
				return true;
			}
		}
	}
	return false;
}
#endif


static unsigned alarms_loop(void *)
{
	static uint8_t last_m = 0xf0;
	uint8_t h,m,s,d,md,mon;
	unsigned y;
	get_time_of_day(&h,&m,&s,&d,&md,&mon,&y);
	if ((h > 23) || (m == last_m) || (y == 1970))
		return 300;
	last_m = m;
	if (!Config.actions_enable())
		return 2000;
	//dbug("alarmclock() check %s %u:%02u",WeekDay_str((WeekDay)d),h,m);
	const vector<AtAction> &atactions = Config.at_actions();
	for (size_t i = 0; i < atactions.size(); ++i) {
		const AtAction &a = atactions[i];
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
		if (!x)
			continue;
		const char *aname = a.action().c_str();
		size_t l = strlen(aname);
		char action[l+1];
		memcpy(action,aname,l+1);
		log_dbug(TAG,"at %s %u:%02u => %s",Weekdays_de[wd],a.min_of_day()/60,a.min_of_day()%60,aname);
		const char *arg = 0;
		char *c = strchr(action,' ');
		if (c) {
			*c = 0;
			arg = c+1;
		}
		if (strchr(aname,'`')) {
			if (event_t e = event_id(aname)) {
				if (arg)
					event_trigger_arg(e,strdup(arg));
				else
					event_trigger(e);
			} else {
				log_warn(TAG,"unknown event '%s'",aname);
			}
		} else if (arg) {
			if (action_activate_arg(aname,(void*)arg))
				log_warn(TAG,"unknown action '%s'",aname);
		} else {
			if (action_activate(aname))
				log_warn(TAG,"unknown action '%s'",aname);
		}
	}
	return 200;
}


static void alarms_set(void *a)
{
	Enabled->set((bool)a);
}


static void alarms_toggle(void *)
{
	Enabled->set(!Enabled->get());
}


bool alarms_enabled()
{
	return Enabled->get();
}


void alarms_setup()
{
	Enabled = RTData->add("timers_enabled",(bool)Config.actions_enable());
	action_add("timer!enable",alarms_set,(void*)1,"enable 'at' execution");
	action_add("timer!disable",alarms_set,0,"disable 'at' execution");
	action_add("timer!toggle",alarms_toggle,0,"toggle 'at' execution");
	cyclic_add_task("alarms",alarms_loop);
}


#ifdef CONFIG_HOLIDAYS
static int parse_date(const char *arg, unsigned *d)
{
	int n = sscanf(arg,"%u.%u.%u",d,d+1,d+2);
	if (n < 2) {
		n = sscanf(arg,"%u/%u/%u",d+1,d,d+2);
		if (n < 2) {
			n = sscanf(arg,"%u-%u-%u",d+2,d+1,d);
			if (n < 3)
				return 1;
		}
	}
	if ((d[1] <= 0) || (d[1] > 12))
		return -1;
	if ((d[0] <= 0) || (d[0] > 31))
		return -1;
	return n;
}


const char *holiday(Terminal &t, int argc, const char *args[])
{
	if ((argc == 1) || (argc > 3))
		return "Invalid number of arguments.";
	if ((argc == 3) && (!strcmp(args[1],"-D"))) {
		long l = strtol(args[2],0,10);
		if ((l < 0) || (l >= (long)Config.holidays_size()))
			return "Argument out of range.";
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
		for (size_t i = 0; i < Config.holidays_size(); ++i) {
			Config.holidays(i).toJSON(t);
			if (i + 1 != Config.holidays_size())
				t << ',';
		}
		t.print("]}\n");
		return 0;
	}
#if 1
	unsigned date[3];
	int n = parse_date(args[1],date);
	if (n < 2)
		return "Invalid argument #1.";
	Date h;
	h.set_month(date[1]);
	h.set_day(date[0]);
	if ((n == 3) && (date[2] != 0))
		h.set_year(date[2]);
#else
	int y,m,d;
	int n = sscanf(args[1],"%d.%d.%d",&d,&m,&y);
	if (n < 2) {
		n = sscanf(args[1],"%d/%d/%d",&m,&d,&y);
		if (n < 2) {
			n = sscanf(args[1],"%d-%d-%d",&y,&m,&d);
			if (n < 3)
				return "Invalid argument #1.";
		}
	}
	if ((m <= 0) || (m > 12)) {
		t.println("invalid month");
		return 1;
	}
	if ((d <= 0) || (d > 31)) {
		t.println("invalid day");
		return 1;
	}
	Date h;
	h.set_month(m);
	h.set_day(d);
	if ((n == 3) && (y != 0))
		h.set_year(y);
#endif
	if (argc == 3) {
#if 1
		unsigned enddate[3];
		n = parse_date(args[2],enddate);
		if (n < 2)
			return "Invalid argument #2.";
		h.set_endday(enddate[0]);
		h.set_endmonth(enddate[1]);
		h.set_endyear(enddate[2]-h.year());
#else
		n = sscanf(args[2],"%d.%d.%d",&d,&m,&y);
		if (n < 3) {
			n = sscanf(args[2],"%d/%d/%d",&m,&d,&y);
			if (n < 3) {
				n = sscanf(args[2],"%d-%d-%d",&y,&m,&d);
				if (n < 3) {
					t.println("invalid input format");
					return 1;
				}
			}
		}
		if ((m <= 0) || (m > 12)) {
			t.println("invalid month");
			return 1;
		}
		if ((d <= 0) || (d > 31)) {
			t.println("invalid day");
			return 1;
		}
		h.set_endday(d);
		h.set_endmonth(m);
		h.set_endyear(y-h.year());
#endif
	}
	*Config.add_holidays() = h;
	return 0;
}
#endif


const char *at(Terminal &t, int argc, const char *args[])
{
	if (argc > 5)
		return "Invalid number of arguments.";
	if (argc == 1) {
		return help_cmd(t,args[0]);
	}
	if (argc == 2) {
		if (!strcmp(args[1],"-l")) {
			for (size_t i = 0; i < Config.at_actions_size(); ++i) {
				const AtAction &a = Config.at_actions(i);
				const char *name = a.action().c_str();
				Action *x = action_get(name);
				const char *text = x ? x->text : "<unknown action>";
				t.printf("\t[%d]: %-10s %2u:%02u  %-16s '%s'%s\n"
					,i
					,WeekDay_str(a.day())
					,a.min_of_day()/60
					,a.min_of_day()%60
					,name
					,text
					,a.enable()?"":" (disabled)");
			}
		} else if (!strcmp(args[1],"-0")) {
			Config.set_actions_enable(Config.actions_enable()&~1);
			if (!Enabled->get())
				return "Already disabled.";
			Enabled->set(false);
		} else if (!strcmp(args[1],"-1")) {
			Config.set_actions_enable(Config.actions_enable()|1);
			if (Enabled->get())
				return "Already enabled.";
			Enabled->set(true);
		} else if (!strcmp(args[1],"-s")) {
			cfg_store_nodecfg();
		} else if (!strcmp(args[1],"-j")) {
			t.printf("{\"alarms\":[");
			for (size_t i = 0; i < Config.at_actions_size(); ++i) {
				Config.at_actions(i).toJSON(t);
				if (i + 1 != Config.at_actions_size())
					t << ',';
			}
			t.print("]}\n");
		} else {
			return "Invalid argument #1.";
		}
		return 0;
	}
	if (argc == 3) {
		bool all = !strcmp(args[2],"all");
		long id = strtol(args[2],0,10);
		if ((id == 0) && ((args[2][0] < '0') || (args[2][0] > '9')))
			id = all ? 0 : -1;
		if ((id >= (long)Config.at_actions_size()) || (id < 0))
			return "Invalid argument #2.";
		if (!strcmp(args[1],"-e") || !strcmp(args[1],"-d")) {
			bool enable = args[1][1] == 'e';
			if (!all) {
				Config.mutable_at_actions(id)->set_enable(enable);
			} else {
				for (size_t i = 0; i < Config.at_actions_size(); ++i)
					Config.mutable_at_actions(id)->set_enable(enable);
			}
		}
		if (!strcmp(args[1],"-D")) {
			if (all)
				Config.clear_at_actions();
			else
				Config.mutable_at_actions()->erase(Config.mutable_at_actions()->begin()+id);
		} else {
			return "Invalid argument #1.";
		}
		return 0;
	}
	bool have_day = false;
	WeekDay wd;
	for (size_t i = 0; i < weekdays_max; ++i) {
		if (0 == strcasecmp(Weekdays_de[i],args[1])) {
			wd = (WeekDay)i;
			have_day = true;
			break;
		}
	}
	if (!have_day) {
		for (size_t i = 0; i < weekdays_max; ++i) {
			if (0 == strcmp(Weekdays_en[i],args[1])) {
				wd = (WeekDay)i;
				have_day = true;
				break;
			}
		}
		if (!have_day)
			return "Invalid argument #1.";
	}
	int h,m;
	int n = sscanf(args[2],"%d:%d",&h,&m);
	if ((2 != n) || (h < 0) || (h > 23) || (m < 0) || (m > 59)) {
		return "Invalid argument #2.";
	}
	if (0 == action_get(args[3]))
		return "Invalid argument #3.";
	AtAction *a = Config.add_at_actions();
	a->set_min_of_day(h*60+m);
	a->set_day(wd);
	a->set_enable(true);
	if (argc == 4) {
		a->set_action(args[3]);
	} else {
		size_t l0 = strlen(args[3]);
		size_t l1 = strlen(args[4]);
		char buf[l0+l1+2];
		memcpy(buf,args[3],l0);
		buf[l0] = ' ';
		memcpy(buf+l0+1,args[4],l1+1);
		a->set_action(buf);
	}
	return 0;
}

#endif // CONFIG_AT_ACTIONS
