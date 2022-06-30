/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#ifdef CONFIG_DISPLAY

#include "actions.h"
#include "cyclic.h"
#include "display.h"
#include "fonts_ssd1306.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "env.h"

#include <sys/time.h>
#include <time.h>

typedef enum clockmode {
	cm_version,
	cm_time,
	cm_date,
	cm_stopwatch,
	CLOCK_MODE_MAX,
} clockmode_t;


struct Screen
{
	TextDisplay *disp;
	uint32_t sw_start = 0, sw_delta = 0, sw_pause = 0, modestart = 0;
	uint8_t display[8];
	uint8_t digits;
	clockmode_t mode = cm_time;
	bool modech = false;

	void display_time();
	void display_date();
	void display_data();
	void display_sw();
	void display_version();
};

extern const char Version[];

#define TAG MODULE_SCREEN
static Screen *Ctx = 0;

static void sw_startstop(void *arg)
{
	uint32_t now = uptime();
	if (Ctx->sw_start) {
		Ctx->sw_delta = now-Ctx->sw_start;
		Ctx->sw_start = 0;
	} else {
		Ctx->sw_start = uptime() - Ctx->sw_delta;
		Ctx->sw_delta = 0;
	}
}


static void sw_clear(void *arg)
{
	Ctx->sw_start = 0;
	Ctx->sw_delta = 0;
	Ctx->sw_pause = 0;
}


static void sw_pause(void *arg)
{
	if (Ctx->sw_pause == 0)
		Ctx->sw_pause = uptime() - Ctx->sw_start;
	else
		Ctx->sw_pause = 0;
}


static void switch_mode(void *arg)
{
	const char *m = (const char *) arg;
	if (arg == 0) {
		Ctx->mode = (clockmode_t)((int)Ctx->mode + 1);
		if (Ctx->mode >= CLOCK_MODE_MAX) {
			size_t n = RTData->numChildren();
			size_t x = Ctx->mode - CLOCK_MODE_MAX;
			EnvElement *e;
			do {
				e = RTData->getChild(x);
				if (e) {
					const char *n = e->name();
					if (e->toObject() || (0 == strcmp(n,"version")) || (0 == strcmp(n,"ltime")) || (0 == strcmp(n,"uptime"))) {
						e = 0;
						++x;
					}
				}
			} while ((e == 0) && (x < n));
			if (x == n)
				Ctx->mode = cm_version;
			else
				Ctx->mode = (clockmode_t) (x+CLOCK_MODE_MAX);
		}
	} else if (0 == strcmp("time",m)) {
		Ctx->mode = cm_time;
	} else if (0 == strcmp("date",m)) {
		Ctx->mode = cm_date;
	} else if (0 == strcmp("stopwatch",m)) {
		Ctx->mode = cm_stopwatch;
	} else {
		int idx = RTData->getOffset((const char *) arg);
		if (idx == -1) {
			log_warn(TAG,"invalid mode request %s",(const char *) arg);
			return;
		}
		Ctx->mode = (clockmode_t) (idx+CLOCK_MODE_MAX);
	}
	log_dbug(TAG,"new mode %d",Ctx->mode);
	Ctx->modestart = uptime();
	Ctx->modech = true;
}

static void bright_set(void *arg)
{
	if (arg) {
		long l = strtol((const char *)arg, 0, 0);
		int m = Ctx->disp->maxDim();
		if ((l >= 0) && (l <= m))
			Ctx->disp->setDim(l);
		else
			log_warn(TAG,"maximum brightness %u",l);
	}
}


static void shutdown(void *arg)
{
	Ctx->disp->setOn(false);
}

static void poweron(void *arg)
{
	Ctx->disp->setOn(true);
}


void Screen::display_time()
{
	uint8_t h=0,m=0,s=0;
	unsigned y=0;
	get_time_of_day(&h,&m,&s,0,0,0,&y);
	bool colon = disp->hasChar(':');
	if (y < 2000) {
		if (colon)
			disp->write("--:--:--");
		else
			disp->write("--.--.--");
		return;
	}
	disp->writeHex(h/10);
	disp->writeHex(h%10,!colon);
	if (colon)
		disp->write(":");
	disp->writeHex(m/10);
	disp->writeHex(m%10,digits>=6?!colon:false);
	if ((colon && (digits >= 8)) || (digits >= 6)) {
		if (colon)
			disp->write(":");
		disp->writeHex(s/10);
		disp->writeHex(s%10);
	}
}


void Screen::display_date()
{
	uint8_t wd=0,day=0,mon=0;
	unsigned year=0;
	get_time_of_day(0,0,0,&wd,&day,&mon,&year);
	if (year < 2000) {
		disp->write("--.--.----");
		return;
	}
	disp->writeHex(day/10);
	disp->writeHex(day%10,true);
	disp->writeHex(mon/10);
	disp->writeHex(mon%10,true);
	if (digits >= 8) {
		disp->writeHex(year/1000);
		disp->writeHex((year%1000)/100);
	}
	if (digits >= 6) {
		year %= 100;
		disp->writeHex(year/10);
		disp->writeHex(year%10);
	}
}


void Screen::display_version()
{
	if (DotMatrix *dm = disp->toDotMatrix()) {
		dm->drawRect(0,0,dm->maxX(),dm->maxY());
		dm->setXY(10,1);
		dm->setFont(font_sanslight16);
		dm->write("Atrium");
		dm->setFont(font_sanslight12);
		dm->setXY(10,23);
		const char *sp = strchr(Version,' ');
		dm->write(Version,sp-Version);
		if (dm->maxY() >= 48) {
			dm->setXY(10,48);
			dm->setFont(font_tomthumb);
			dm->write("(C) 2021, T. Maier-Komor");
		}
		return;
	}
	const char *sp = strchr(Version,' ');
	disp->setPos(0,0);
	unsigned cpl = disp->charsPerLine();
	unsigned nl = disp->numLines();
	if (disp->hasAlpha() && (cpl > 6) && (nl > 1)) {
		disp->write("Atrium ");
		if (cpl < 12)
			disp->setPos(0,1);
		disp->write(Version,sp-Version);
		if (cpl < 12)
			return;
		disp->setPos(0,1);
		if (cpl >= 20)
			disp->write("(C) 2021 Maier-Komor");
		else
			disp->write("(C) Maier-Komor");
		if (nl > 2)
			disp->write(sp+1);
		disp->setPos(0,nl+1);
	} else {
		disp->write(Version+1,sp-Version-1);
	}
}


void Screen::display_sw()
{
	bool colon = disp->hasChar(':');
	uint32_t dt;
	if (sw_pause) {
		dt = sw_pause;
	} else if (sw_start) {
		uint32_t now = uptime();
		dt = now - sw_start;
	} else {
		dt = Ctx->sw_delta;
	}
	dt /= 10;
	uint8_t hsec = dt%10;
	dt /= 10;
	uint8_t dsec = dt%10;
	dt /= 10; // now in sec
	uint8_t h = dt / 3600;
	dt -= h * 3600;
	uint8_t m = dt / 60;
	dt -= m * 60;
	uint8_t s = dt;
	if (digits >= 8) {
		disp->writeHex(h/10);
		disp->writeHex(h%10,!colon);
		if (colon)
			disp->write(":");
		disp->writeHex(m/10);
		disp->writeHex(m%10,!colon);
		if (colon)
			disp->write(":");
		disp->writeHex(s/10);
		disp->writeHex(s%10,true);
		disp->writeHex(dsec);
		disp->writeHex(hsec);
	} else if (digits == 6) {
		if (h) {
			disp->writeHex(h/10);
			disp->writeHex(h%10,true);
			disp->writeHex(m/10);
			disp->writeHex(m%10,true);
			disp->writeHex(s/10);
			disp->writeHex(s%10);
		} else {
			disp->writeHex(m/10);
			disp->writeHex(m%10,true);
			disp->writeHex(s/10);
			disp->writeHex(s%10,true);
			disp->writeHex(dsec);
			disp->writeHex(hsec);
		}
	} else if (h != 0) {
		disp->writeHex(h/10);
		disp->writeHex(h%10,true);
		disp->writeHex(m/10);
		disp->writeHex(m%10);
	} else if (m != 0) {
		disp->writeHex(m/10);
		disp->writeHex(m%10,true);
		disp->writeHex(s/10);
		disp->writeHex(s%10);
	} else {
		disp->writeHex(s/10);
		disp->writeHex(s%10,true);
		disp->writeHex(dsec);
		disp->writeHex(hsec);
	}
}


static unsigned clock_iter(void *arg)
{
	bool alpha = Ctx->disp->hasAlpha();
	Screen *Ctx = (Screen *)arg;
	if (Ctx->modech) {
		Ctx->disp->clear();
		Ctx->modech = false;
		if (alpha) {
			const char *text = 0;
			uint8_t nextFont = font_sans12;
			switch (Ctx->mode) {
			case cm_time:
				text = "time of day";
//				nextFont = 4;
				break;
			case cm_date:
				text = "date";
				break;
			case cm_version:
				Ctx->display_version();
				break;
			case cm_stopwatch:
				text = "stop-watch";
				nextFont = font_mono9;
				break;
			default:
				if (EnvElement *e = RTData->getChild(Ctx->mode-CLOCK_MODE_MAX))
					text = e->name();
			}
			if (text) {
				log_dbug(TAG,"mode %s",text);
				DotMatrix *dm = Ctx->disp->toDotMatrix();
				if (dm) {
					dm->setFont(font_sanslight10);
					dm->setXY(3,4);
				} else {
					Ctx->disp->setPos(0,0);
				}
				Ctx->disp->write(text);
				if (dm) {
					dm->setFont(nextFont);
					dm->setXY(3,12);
				}
			}
			if (Ctx->disp->numLines() > 1) {
				Ctx->disp->setPos(0,1);
				Ctx->modestart -= 1000;
			}
			Ctx->disp->sync();
			return 50;
		}
	}
	uint32_t now = uptime();
	if (alpha && (now - Ctx->modestart < 800))
		return 50;
	unsigned d = 100;
//	Ctx->disp->setPos(0,Ctx->disp->numLines() > 1 ? 1 : 0);
	Ctx->disp->write("\r");
	Ctx->disp->clrEol();
	double v = 0;
	const char *fmt = 0, *dim = 0;
	switch (Ctx->mode) {
	case cm_date:
		if ((uptime() - Ctx->modestart) < 5000) {
			Ctx->display_date();
			d = 500;
			break;
		}
		log_dbug(TAG,"auto date->clock");
		Ctx->mode = cm_time;
		Ctx->modech = true;
		d = 20;
		break;
	case cm_time:
		Ctx->display_time();
		break;
	case cm_version:
		{
			uint32_t ut = uptime();
			time_t now;
			time(&now);
			if ((now > 3600*24*365*30) && (ut - Ctx->modestart > 1000)) {
				Ctx->mode = cm_time;
				Ctx->modech = true;
				return 20;
			}
			return 500;
		}
		break;
	case cm_stopwatch:
		if (Ctx->sw_pause == 0)
			Ctx->display_sw();
		d = 20;
		break;
	default:
		if (EnvElement *e = RTData->getChild(Ctx->mode-CLOCK_MODE_MAX)) {
			dim = e->getDimension();
			if (EnvNumber *n = e->toNumber()) {
				fmt = n->getFormat();
				v = n->get();
				log_dbug(TAG,"%s %s %f",e->name(),fmt,v);
				if (isnan(v))
					fmt = "---";
			} else if (EnvBool *b = e->toBool()) {
				fmt = b->get() ? "on" : "off";
			} else if (EnvString *s = e->toString()) {
				fmt = s->get();
				log_dbug(TAG,"string %s %s",e->name(),fmt);
			} else {
				fmt = "";
			}
		}
	}
	if (fmt) {
		if (isnan(v)) {
			if (Ctx->disp->hasAlpha())
				Ctx->disp->write("n/a");
			else
				Ctx->disp->write("----");
		} else {
			char buf[9];
			snprintf(buf,sizeof(buf),fmt,v);
			log_dbug(TAG,"write %s",buf);
			Ctx->disp->write(buf);
			if (dim) {
				if (Ctx->disp->charsPerLine() > 6)
					Ctx->disp->write(" ");
				Ctx->disp->write(dim);
			} else {
				Ctx->disp->clrEol();
			}
		}
	}
	Ctx->disp->sync();
	return d;
}


int screen_setup()
{
	TextDisplay *d = TextDisplay::getFirst();
	if (d == 0)
		return 0;
	Ctx = new Screen;
	Ctx->disp = d;
	Ctx->digits = d->charsPerLine();
	d->setCursor(false);
	d->setBlink(false);
	action_add("display!set_mode",switch_mode,0,"switch to next or specified display mode");
	if (d->maxDim() > 1) {
		action_add("display!set_bright",bright_set,(void*)d,"set brightness to argument value");
	}
	action_add("display!off",shutdown,Ctx,"clock: turn off");
	action_add("display!on",poweron,Ctx,"clock: turn on");
	action_add("sw!startstop",sw_startstop,Ctx,"stopwatch start/stop");
	action_add("sw!reset",sw_clear,Ctx,"stopwatch reset");
	action_add("sw!pause",sw_pause,Ctx,"stopwatch pause/resume");
	shutdown(Ctx);
	poweron(Ctx);
	d->setDim(d->maxDim());
	Ctx->mode = cm_version;
	Ctx->modech = true;
	d->clear();
	cyclic_add_task("display", clock_iter, Ctx);
	log_info(TAG,"ready");
	return 0;
}

#endif
