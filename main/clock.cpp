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

#include <sdkconfig.h>

#ifdef CONFIG_DISPLAY

#include "actions.h"
#include "cyclic.h"
#include "display.h"
#include "fonts_ssd1306.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "timefuse.h"
#include "ujson.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>

#include <sys/time.h>
#include <time.h>

typedef enum clockmode {
	cm_version,
	cm_time,
	cm_date,
	cm_stopwatch,
	cm_temperature,
	cm_humidity,
	cm_pressure,
	cm_gasr,
	cm_co2,
	cm_tvoc,
	cm_lux,
	cm_prox,
	cm_display_bin,	// display something - e.g. alarm set value, binary data
	cm_display_dec,	// display something - e.g. alarm set value, decimal data
	CLOCK_MODE_MAX,
} clockmode_t;


struct Clock
{
	TextDisplay *disp;
	uint32_t sw_start = 0, sw_delta = 0, sw_pause = 0;
	uint8_t digits, display[8];
	uint32_t modestart = 0;
	clockmode_t mode = cm_time;
	bool modech = false;

	void display_time();
	void display_date();
	void display_data();
	void display_sw();
	void display_version();
};

extern const char Version[];

static const char TAG[] = "clock";
static Clock *ctx = 0;

static void sw_startstop(void *arg)
{
	uint32_t now = uptime();
	Clock *ctx = (Clock *)arg;
	if (ctx->sw_start) {
		ctx->sw_delta = now-ctx->sw_start;
		ctx->sw_start = 0;
	} else {
		ctx->sw_start = uptime() - ctx->sw_delta;
		ctx->sw_delta = 0;
	}
}


static void sw_clear(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->sw_start = 0;
	ctx->sw_delta = 0;
	ctx->sw_pause = 0;
}


static void sw_pause(void *arg)
{
	Clock *ctx = (Clock *)arg;
	if (ctx->sw_pause == 0)
		ctx->sw_pause = uptime() - ctx->sw_start;
	else
		ctx->sw_pause = 0;
}


static void switch_mode(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	log_dbug(TAG,"set mode %d",ctx->mode);
	if (ctx->mode == cm_temperature) {
		if (0 == RTData->get("temperature"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_humidity) {
		if (0 == RTData->get("humidity"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_pressure) {
		if (0 == RTData->get("pressure"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_gasr) {
		if (0 == RTData->get("gasresistance"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_co2) {
		if (0 == RTData->get("CO2"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_tvoc) {
		if (0 == RTData->get("TVOC"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_lux) {
		if (0 == RTData->get("lux"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode == cm_prox) {
		if (0 == RTData->get("prox"))
			ctx->mode = (clockmode_t)((int)ctx->mode + 1);
	}
	if (ctx->mode >= cm_display_bin)
		ctx->mode = cm_time;
	log_dbug(TAG,"use mode %d",ctx->mode);
	ctx->modestart = uptime();
	ctx->modech = true;
}


static void temp_mode(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->mode = cm_temperature;
	ctx->modech = true;
}


static void time_mode(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->mode = cm_time;
	ctx->modech = true;
}


/*
static void bright_dec(void *arg)
{
	TextDisplay *disp = (TextDisplay *)arg;
	uint8_t dim = disp->getDim();
	if (dim != 0)
		disp->setDim(dim-1);
}


static void bright_inc(void *arg)
{
	TextDisplay *disp = (TextDisplay *)arg;
	uint8_t dim = disp->getDim();
	if (dim != disp->maxDim())
		disp->setDim(dim+1);
}
*/


static void shutdown(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->disp->setOn(false);
}

static void poweron(void *arg)
{
	Clock *ctx = (Clock *)arg;
	ctx->disp->setOn(true);
}


void Clock::display_time()
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


void Clock::display_date()
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


void Clock::display_version()
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
	} else {
		disp->write(Version+1,sp-Version-1);
	}
}


void Clock::display_sw()
{
	bool colon = disp->hasChar(':');
	uint32_t dt;
	if (sw_pause) {
		dt = sw_pause;
	} else if (sw_start) {
		uint32_t now = uptime();
		dt = now - sw_start;
	} else {
		dt = ctx->sw_delta;
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


void Clock::display_data()
{
	// TODO
//	for (int i = 0; i < digits; ++i)
//		ctx->disp->setDigit(ctx->digits-i-1,ctx->display[i]);
}


static unsigned clock_iter(void *arg)
{
	bool alpha = ctx->disp->hasAlpha();
	Clock *ctx = (Clock *)arg;
	if (ctx->modech) {
		ctx->disp->clear();
		ctx->modech = false;
		if (alpha) {
			const char *text = 0;
			uint8_t nextFont = font_sans12;
			switch (ctx->mode) {
			case cm_display_bin:
			case cm_display_dec:
				break;
			case cm_time:
				text = "time of day";
//				nextFont = 4;
				break;
			case cm_date:
				text = "date";
				break;
			case cm_version:
				ctx->display_version();
				break;
			case cm_temperature:
				text = "temperature";
				break;
			case cm_humidity:
				text = "humidity";
				break;
			case cm_pressure:
				text = "pressure";
				break;
			case cm_gasr:
				text = "gas-R";
				break;
			case cm_co2:
				text = "CO2";
				break;
			case cm_tvoc:
				text = "TVOC";
				break;
			case cm_lux:
				text = "lux";
				break;
			case cm_prox:
				text = "proximity";
				break;
			case cm_stopwatch:
				text = "stop-watch";
				nextFont = font_mono9;
				break;
			default:
				abort();
			}
			if (text) {
				log_dbug(TAG,"mode %s",text);
				DotMatrix *dm = ctx->disp->toDotMatrix();
				if (dm) {
					dm->setFont(font_sanslight10);
					dm->setXY(3,4);
				} else {
					ctx->disp->setPos(0,0);
				}
				ctx->disp->write(text);
				if (dm) {
					dm->setFont(nextFont);
					dm->setXY(3,12);
				}
			}
			if (ctx->disp->numLines() > 1) {
				ctx->disp->setPos(0,1);
				ctx->modestart -= 1000;
			}
			ctx->disp->sync();
			return 50;
		}
	}
	uint32_t now = uptime();
	if (alpha && (now - ctx->modestart < 800))
		return 50;
	unsigned d = 100;
//	ctx->disp->setPos(0,ctx->disp->numLines() > 1 ? 1 : 0);
	ctx->disp->write("\r");
	ctx->disp->clrEol();
	double v;
	const char *fmt = 0, *dim = 0;
	switch (ctx->mode) {
	case cm_date:
		if ((uptime() - ctx->modestart) < 5000) {
			ctx->display_date();
			d = 500;
			break;
		}
		log_dbug(TAG,"auto date->clock");
		ctx->mode = cm_time;
		ctx->modech = true;
		d = 20;
		break;
	case cm_time:
		ctx->display_time();
		break;
	case cm_version:
		{
			uint32_t ut = uptime();
			time_t now;
			time(&now);
			if ((now > 3600*24*365*30) && (ut - ctx->modestart > 1000)) {
				ctx->mode = cm_time;
				ctx->modech = true;
				return 20;
			}
			return 500;
		}
		break;
	case cm_stopwatch:
		if (ctx->sw_pause == 0)
			ctx->display_sw();
		d = 20;
		break;
	case cm_temperature:
		v = RTData->get("temperature")->toNumber()->get();
		fmt = "%4.1f";
		dim = "\260C";
		break;
	case cm_humidity:
		v = RTData->get("humidity")->toNumber()->get();
		fmt = "%4.1f";
		dim = "%";
		break;
	case cm_pressure:
		v = RTData->get("pressure")->toNumber()->get();
		fmt = "%4.1f";
		dim = "hPa";
		break;
	case cm_gasr:
		v = RTData->get("gasresistance")->toNumber()->get();
		fmt = "%4.1f";
		if (alpha)
			dim = "kO";
		break;
	case cm_co2:
		v = RTData->get("CO2")->toNumber()->get();
		fmt = "%3.0f";
		dim = "ppm";
		break;
	case cm_tvoc:
		v = RTData->get("TVOC")->toNumber()->get();
		fmt = "%4.0f";
		dim = "ppb";
		break;
	case cm_lux:
		v = RTData->get("lux")->toNumber()->get();
		fmt = "%4.0f";
		dim = 0;
		break;
	case cm_prox:
		v = RTData->get("prox")->toNumber()->get();
		fmt = "%4.0f";
		dim = 0;
		break;
	case cm_display_bin:
	case cm_display_dec:
		ctx->display_data();
		break;
	default:
		abort();
	}
	if (fmt) {
		if (isnan(v)) {
			if (ctx->disp->hasAlpha())
				ctx->disp->write("n/a");
			else
				ctx->disp->write("----");
		} else {
			char buf[8];
			sprintf(buf,fmt,v);
			ctx->disp->write(buf);
			if (dim) {
				if (ctx->disp->charsPerLine() > 6)
					ctx->disp->write(" ");
				ctx->disp->write(dim);
			} else {
				ctx->disp->clrEol();
			}
		}
	}
	ctx->disp->sync();
	return d;
}


int clockapp_setup()
{
	TextDisplay *d = TextDisplay::getFirst();
	if (d == 0)
		return 0;
	ctx = new Clock;
	ctx->disp = d;
	ctx->digits = d->charsPerLine();
	d->setCursor(false);
	d->setBlink(false);
	action_add("clock!switch_mode",switch_mode,ctx,"switch to next clock mode");
	action_add("clock!time_mode",time_mode,ctx,"switch to display time display");
	action_add("clock!temp_mode",temp_mode,(void*)ctx,"switch to display temperature");
//	action_add("clock!bright_inc",bright_inc,(void*)d,"clock: increase brightness");
//	action_add("clock!bright_dec",bright_dec,(void*)d,"clock: decrease brightness");
	action_add("clock!off",shutdown,ctx,"clock: turn off");
	action_add("clock!on",poweron,ctx,"clock: turn on");
	action_add("sw!startstop",sw_startstop,ctx,"stopwatch start/stop");
	action_add("sw!reset",sw_clear,ctx,"stopwatch reset");
	action_add("sw!pause",sw_pause,ctx,"stopwatch pause/resume");
	shutdown(ctx);
	poweron(ctx);
	d->setDim(d->maxDim());
	ctx->mode = cm_version;
	ctx->modech = true;
	d->clear();
	log_dbug(TAG,"start");
	cyclic_add_task("clock", clock_iter, ctx);
	return 0;
}

#endif
