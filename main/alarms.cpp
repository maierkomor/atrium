/*
 *  Copyright (C) 2017-2021, Thomas Maier-Komor
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
#include "swcfg.h"
#include "cyclic.h"
#include "event.h"
#include "ujson.h"
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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#ifdef CONFIG_IDF_TARGET_ESP32
#include <rom/gpio.h>
#endif

#include <vector>

using namespace std;

const char *Weekdays_en[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa", "wd", "we", "ed", "hd" };
const char *Weekdays_de[] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa", "wt", "we", "jt", "ft" };
static const char TAG[] = "alarms";
static JsonBool *Enabled = 0;
JsonString *Localtime = 0;

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
			log_dbug(TAG,"direct match d=%u, m=%u, y=%u, sd=%u, sm=%u",d,m,y,sd,sm);
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
				log_dbug(TAG,"match so = %x, eo = %x",so,eo);
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
	char now[32];
	sprintf(now,"%s, %u:%02u",Weekdays_de[d],h,m);
	Localtime->set(now);
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
		if (x) {
			const char *aname = a.action().c_str();
			if (strchr(aname,'`')) {
				event_t e = event_id(aname);
				if (e == 0)
					e = event_register(aname);
				log_dbug(TAG,"alarm at %s %u:%02u triggers event %s",Weekdays_de[wd],a.min_of_day()/60,a.min_of_day()%60,aname);
				event_trigger(e);

			} else {
				log_dbug(TAG,"alarm at %s %u:%02u triggers action %s",Weekdays_de[wd],a.min_of_day()/60,a.min_of_day()%60,aname);
				if (action_activate(aname))
					log_warn(TAG,"unable to execute unknown action '%s'",aname);
			}
		}
	}
	return 300;
}


static void alarms_set(void *a)
{
	if (Enabled)
		Enabled->set((bool)a);
}


static void alarms_toggle(void *)
{
	if (Enabled)
		Enabled->set(!Enabled->get());
}


bool alarms_enabled()
{
	return Enabled && Enabled->get();
}


int alarms_setup()
{
	Enabled = new JsonBool("timers_enabled",Config.actions_enable());
	RTData->append(Enabled);
	Localtime = new JsonString("ltime","");
	RTData->append(Localtime);
	action_add("timer!enable",alarms_set,(void*)1,"enable timer based triggers");
	action_add("timer!disable",alarms_set,0,"disable timer based triggers");
	action_add("timer!toggle",alarms_toggle,0,"toggle timer based triggers");
	return cyclic_add_task("alarms",alarms_loop);
}


#ifdef CONFIG_HOLIDAYS
int holiday(Terminal &t, int argc, const char *args[])
{
	if ((argc == 1) || (argc > 3))
		return arg_invnum(t);
	if ((argc == 3) && (!strcmp(args[1],"-D"))) {
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
		for (size_t i = 0; i < Config.holidays_size(); ++i) {
			Config.holidays(i).toJSON(t);
			if (i + 1 != Config.holidays_size())
				t << ',';
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
			if (n < 2)
				return arg_invalid(t,args[1]);
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


int at(Terminal &t, int argc, const char *args[])
{
	if (argc > 4)
		return arg_invnum(t);
	if (argc == 1) {
		return help_cmd(t,args[0]);
	}
	if (argc == 2) {
		if (!strcmp(args[1],"-l")) {
			for (size_t i = 0; i < Config.at_actions_size(); ++i) {
				const AtAction &a = Config.at_actions(i);
				const char *name = a.action().c_str();
				Action *x = action_get(name);
				const char *text = x ? x->text : "<action not found>";
				t.printf("\t[%d]: %-10s %2u:%02u  %-16s '%s'%s\n"
					,i
					,WeekDay_str(a.day())
					,a.min_of_day()/60
					,a.min_of_day()%60
					,name
					,text?text:"<unnamed action>"
					,a.enable()?"":" (disabled)");
			}
		} else if (!strcmp(args[1],"-0")) {
			Config.set_actions_enable(Config.actions_enable()&~1);
			Enabled->set(false);
			t.printf("timers disabled\n");
		} else if (!strcmp(args[1],"-1")) {
			Config.set_actions_enable(Config.actions_enable()|1);
			Enabled->set(true);
			t.printf("timers enabled\n");
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
			return arg_invalid(t,args[1]);
		}
		return 0;
	}
	if (argc == 3) {
		bool all = !strcmp(args[2],"all");
		long id = strtol(args[2],0,10);
		if ((id == 0) && ((args[2][0] < '0') || (args[2][0] > '9')))
			id = all ? 0 : -1;
		if ((id >= (long)Config.at_actions_size()) || (id < 0))
			return arg_invalid(t,args[2]);
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
			t.printf("invalid argument\n");
			return arg_invalid(t,args[1]);
		}
		return 0;
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
			if (0 == strcmp(Weekdays_en[i],args[1])) {
				wd = (WeekDay)i;
				have_day = true;
				break;
			}
		}
		if (!have_day)
			return arg_invalid(t,args[1]);
	}
	int h,m;
	int n = sscanf(args[2],"%d:%d",&h,&m);
	if ((2 != n) || (h < 0) || (h > 23) || (m < 0) || (m > 59)) {
		t.printf("invalid time spec (n=%d, h=%d, m=%d)\n",n,h,m);
		return 1;
	}
	if (0 == action_exists(args[3]))
		return arg_invalid(t,args[3]);
	AtAction *a = Config.add_at_actions();
	a->set_min_of_day(h*60+m);
	a->set_action(args[3]);
	a->set_day(wd);
	a->set_enable(true);
	return 0;
}

#endif // CONFIG_AT_ACTIONS
