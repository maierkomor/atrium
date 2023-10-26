/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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
#include "env.h"
#include "fonts.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "luaext.h"
#include "profiling.h"
#include "screen.h"

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

#include <sys/time.h>
#include <time.h>

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
			size_t x = Ctx->mode - CLOCK_MODE_MAX;
			EnvElement *e = RTData->getElement(x);
			while (e) {
				const char *name = e->name();
				if ((0 == strcmp(name,"version")) || (0 == strcmp(name,"ltime")) || (0 == strcmp(name,"uptime"))) {
					++x;
					e = RTData->getElement(x);
					continue;
				}
				const char *pn = e->getParent()->name();
				if ((pn != 0) && (0 == strcmp(pn,"mqtt"))) {
					++x;
					e = RTData->getElement(x);
					continue;
				} else {
					break;
				}
			}
			if (e == 0)
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
	} else if (0 == strcmp("lua",m)) {
		Ctx->mode = cm_lua;
	} else {
		int idx = RTData->getIndex((const char *) arg);
		if (idx == -1) {
			log_warn(TAG,"invalid mode request %s",(const char *) arg);
			return;
		}
		idx += CLOCK_MODE_MAX;
		log_dbug(TAG,"index %d",idx);
		Ctx->mode = (clockmode_t) (idx);
	}
	log_dbug(TAG,"new mode %d",Ctx->mode);
	Ctx->modestart = uptime();
	Ctx->modech = true;
}

static void bright_set(void *arg)
{
	if (arg) {
		long l = strtol((const char *)arg, 0, 0);
		int m = Ctx->disp->maxBrightness();
		if ((l >= 0) && (l <= m))
			Ctx->disp->setBrightness(l);
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
	if (get_time_of_day(&h,&m,&s,0,0,0,&y)) {
		disp->write("--:--:--");
	} else {
		char str[12];
		sprintf(str,"%02u:%02u:%02u",h,m,s);
		disp->write(str);
	}
	/*
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
	*/
}


void Screen::display_date()
{
	uint8_t wd=0,day=0,mon=0;
	unsigned year=0;
	if (get_time_of_day(0,0,0,&wd,&day,&mon,&year)) {
		disp->write("--.--.----");
		return;
	}
	char str[12];
	sprintf(str,"%u.%u.%u",day,mon,year);
	disp->write(str);
	/*
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
	*/
}


void Screen::display_version()
{
	if (MatrixDisplay *dm = disp->toMatrixDisplay()) {
		assert(dm->maxX());
		dm->drawRect(4,1,dm->maxX()-5,dm->maxY()-3);
		dm->setFont(font_sanslight16);
		dm->setPos(10,3);
		dm->write("Atrium");
		dm->setFont(font_sanslight12);
		dm->setPos(10,26);
		const char *sp = strchr(Version,' ');
		dm->write(Version,sp-Version);
		if (dm->maxY() >= 48) {
			dm->setPos(10,48);
			dm->setFont(font_tomthumb);
			dm->write("(C) 2021-2023, T.Maier-Komor");
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
			disp->write("(C) 2023 Maier-Komor");
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
	PROFILE_FUNCTION();
//	bool colon = disp->hasChar(':');
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
	char str[16];
	if (digits >= 8) {
		sprintf(str,"%02u:%02u:%02u.%u%u",h,m,s,dsec,hsec);
	} else if (digits == 6) {
		if (h) {
			sprintf(str,"%02u:%02u:%02u",h,m,s);
		} else {
			sprintf(str,"%02u:%02u.%u%u",m,s,dsec,hsec);
		}
	} else if (h != 0) {
		sprintf(str,"%02u:%02u",h,m);
	} else if (m != 0) {
		sprintf(str,"%02u:%02u",m,s);
	} else {
		sprintf(str,"%02u.%u%u",s,dsec,hsec);
	}
	disp->write(str);
	disp->clrEol();
}


static unsigned clock_iter(void *arg)
{
	bool alpha = Ctx->disp->hasAlpha();
	Screen *Ctx = (Screen *)arg;
	MatrixDisplay *dm = Ctx->disp->toMatrixDisplay();
	if (Ctx->modech) {
		Ctx->disp->clear();
		Ctx->modech = false;
		if (alpha) {
			const char *text = 0;
			uint8_t nextFont = font_sans12;
			switch (Ctx->mode) {
			case cm_time:
				text = "local time";
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
				if (EnvElement *e = RTData->getElement(Ctx->mode-CLOCK_MODE_MAX)) {
					text = e->name();
					log_dbug(TAG,"getElement %d: %s",Ctx->mode,text);
					if (const char *pn = e->getParent()->name()) {
						size_t pl = strlen(pn);
						size_t nl = strlen(text);
						char *tmp = (char *)alloca(pl+nl+2);
						memcpy(tmp,pn,pl);
						tmp[pl] = '/';
						memcpy(tmp+pl+1,text,nl+1);
						text = tmp;
					}
				}
			}
			if (text) {
				log_dbug(TAG,"mode %s",text);
				if (dm) {
					dm->setFont(font_sanslight12);
					dm->setPos(3,4);
				} else {
					Ctx->disp->setPos(0,0);
				}
				Ctx->disp->write(text);
				if (dm) {
					dm->setFont(nextFont);
				}
			}
			if (Ctx->disp->numLines() > 1) {
				Ctx->disp->setPos(0,1);
				Ctx->modestart -= 1000;
			}
			Ctx->disp->flush();
			return 50;
		}
	}
	uint32_t now = uptime();
	if (alpha && (now - Ctx->modestart < 800))
		return 50;
	unsigned d = 100;
//	Ctx->disp->setPos(0,Ctx->disp->numLines() > 1 ? 1 : 0);
	if (dm) {
		dm->setPos(5,22);
	} else {
		Ctx->disp->write("\r");
		Ctx->disp->clrEol();
	}
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
			if ((now > 3600*24*365*30) && (ut - Ctx->modestart > 3000)) {
				Ctx->mode = cm_time;
				Ctx->modech = true;
			}
			return 200;
		}
		break;
	case cm_stopwatch:
		Ctx->display_sw();
		d = 20;
		break;
	case cm_lua:
#ifdef CONFIG_LUA
		d = xlua_render(Ctx);
#else
		Ctx->mode = (clockmode_t)((int)Ctx->mode + 1);
#endif
		break;
	default:
		if (EnvElement *e = RTData->getElement(Ctx->mode-CLOCK_MODE_MAX)) {
			dim = e->getDimension();
			fmt = "";
			if (EnvNumber *n = e->toNumber()) {
				fmt = n->getFormat();
				v = n->get();
				if (isnan(v))
					fmt = "---";
			} else if (EnvBool *b = e->toBool()) {
				fmt = b->get() ? "on" : "off";
			} else if (EnvString *s = e->toString()) {
				fmt = s->get();
			}
//			log_dbug(TAG,"%s %s",e->name(),fmt);
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
//			log_dbug(TAG,"write %s",buf);
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
	Ctx->disp->flush();
	return d;
}


#ifdef CONFIG_LUA
int luax_disp_max_x(lua_State *L)
{
	
	if (TextDisplay *disp = TextDisplay::getFirst()) {
		if (MatrixDisplay *dm = disp->toMatrixDisplay()) {
			lua_pushinteger(L, dm->maxX());
			return 1;
		}
	}
	lua_pushstring(L,"no display");
	lua_error(L);
	return 0;
}


int luax_disp_max_y(lua_State *L)
{
	if (TextDisplay *disp = TextDisplay::getFirst()) {
		if (MatrixDisplay *dm = disp->toMatrixDisplay()) {
			lua_pushinteger(L, dm->maxY());
			return 1;
		}
	}
	lua_pushstring(L,"no display");
	lua_error(L);
	return 0;
}


int luax_disp_set_cursor(lua_State *L)
{
	int x = luaL_checkinteger(L,1);
	int y = luaL_checkinteger(L,2);
	if (TextDisplay *disp = TextDisplay::getFirst()) {
		if (-1 != disp->setPos(x,y))
			return 0;
	}
	lua_pushstring(L,"Invalid argument.");
	lua_error(L);
	return 0;
}


int luax_disp_draw_rect(lua_State *L)
{
	int x = luaL_checkinteger(L,1);
	int y = luaL_checkinteger(L,2);
	int w = luaL_checkinteger(L,3);
	int h = luaL_checkinteger(L,4);
	if (TextDisplay *disp = TextDisplay::getFirst()) {
		if (MatrixDisplay *dm = disp->toMatrixDisplay()) {
			dm->drawRect(x,y,w,h);
			return 0;
		}
	}
	lua_pushstring(L,"no dot-matrix");
	lua_error(L);
	return 0;
}


int luax_disp_set_font(lua_State *L)
{
	const char *fn = luaL_checkstring(L,1);
	if (TextDisplay *disp = TextDisplay::getFirst())
		if (MatrixDisplay *md = disp->toMatrixDisplay())
			if (-1 != md->setFont(fn))
				return 0;
	lua_pushstring(L,"invalid font");
	lua_error(L);
	return 0;
}


int luax_disp_write(lua_State *L)
{
	const char *txt = luaL_checkstring(L,1);
	if (TextDisplay *disp = TextDisplay::getFirst()) {
		disp->write(txt);
	} else {
		lua_pushstring(L,"Invalid argument.");
		lua_error(L);
	}
	return 0;

}


int luax_disp_clear(lua_State *L)
{
	if (TextDisplay *disp = TextDisplay::getFirst()) {
		disp->clear();
	}
	return 0;
}


static const LuaFn Functions[] = {
	{ "disp_max_x", luax_disp_max_x, "get x resolution" },
	{ "disp_max_y", luax_disp_max_y, "get y resolution" },
	{ "disp_draw_rect", luax_disp_draw_rect, "draw recangle (x,y,w,h)" },
	{ "disp_set_cursor", luax_disp_set_cursor, "set cursor position (x,y)" },
	{ "disp_set_font", luax_disp_set_font, "set font (fontname)" },
	{ "disp_write", luax_disp_write, "write text at cursor position with font" },
	{ "disp_clear", luax_disp_clear , "clear the screen" },
	{ 0, 0, 0 }
};

static int luax_fb_setbgcol(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		if (lua_isinteger(L,1))
			md->setBgColorVal(lua_tointeger(L,1));
		else if (lua_isstring(L,1))
			md->setBgColor(color_get(lua_tostring(L,1)));
		return 0;
	}
	lua_pushliteral(L,"fb_setbgcol: no display.");
	lua_error(L);
	return 0;
}


static int luax_fb_setfgcol(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		if (lua_isinteger(L,1))
			md->setFgColorVal(lua_tointeger(L,1));
		else if (lua_isstring(L,1))
			md->setFgColor(color_get(lua_tostring(L,1)));
		return 0;
	}
	lua_pushliteral(L,"fb_setfgcol: no display.");
	lua_error(L);
	return 0;
}


static int luax_fb_setfont(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		if (const char *fn = lua_tostring(L,1)) {
			md->setFont(fn);
		} else {
			md->setFont(lua_tointeger(L,1));
		}
		return 0;
	}
	lua_pushliteral(L,"fb_setfgcol: no display.");
	lua_error(L);
	return 0;
}


static int luax_fb_drawrect(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		int x = luaL_checkinteger(L,1);
		int y = luaL_checkinteger(L,2);
		int w = luaL_checkinteger(L,3);
		int h = luaL_checkinteger(L,4);
		int32_t fgc = -1;
		if (lua_isinteger(L,5))
			fgc = lua_tointeger(L,5);
		else if (lua_isstring(L,5))
			fgc = md->getColor(color_get(lua_tostring(L,5)));
		md->drawRect(x,y,w,h,fgc);
		return 0;
	}
	lua_pushliteral(L,"fb_drawrect: no display.");
	lua_error(L);
	return 0;
}


static int luax_fb_drawline(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		int x = luaL_checkinteger(L,1);
		int y = luaL_checkinteger(L,2);
		int w = luaL_checkinteger(L,3);
		int h = luaL_checkinteger(L,4);
		int32_t fgc = -1;
		if (lua_isinteger(L,5))
			fgc = lua_tointeger(L,5);
		else if (lua_isstring(L,5))
			fgc = md->getColor(color_get(lua_tostring(L,5)));
		md->drawLine(x,y,w,h,fgc);
		return 0;
	}
	lua_pushliteral(L,"fb_drawrect: no display.");
	lua_error(L);
	return 0;
}


static int luax_fb_drawhline(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		int x = luaL_checkinteger(L,1);
		int y = luaL_checkinteger(L,2);
		int l = luaL_checkinteger(L,3);
		int32_t fgc = -1;
		if (lua_isinteger(L,4))
			fgc = lua_tointeger(L,4);
		else if (lua_isstring(L,4))
			fgc = md->getColor(color_get(lua_tostring(L,4)));
		md->drawHLine(x,y,l,fgc);
		return 0;
	}
	lua_pushliteral(L,"fb_drawrect: no display.");
	lua_error(L);
	return 0;
}


static int luax_fb_drawvline(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		int x = luaL_checkinteger(L,1);
		int y = luaL_checkinteger(L,2);
		int l = luaL_checkinteger(L,3);
		int32_t fgc = -1;
		if (lua_isinteger(L,4))
			fgc = lua_tointeger(L,4);
		else if (lua_isstring(L,4))
			fgc = md->getColor(color_get(lua_tostring(L,4)));
		md->drawVLine(x,y,l,fgc);
		return 0;
	}
	lua_pushliteral(L,"fb_drawrect: no display.");
	lua_error(L);
	return 0;
}


static int luax_fb_drawtext(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		int x = luaL_checkinteger(L,1);
		int y = luaL_checkinteger(L,2);
		const char *text = luaL_checkstring(L,3);
		int32_t fgc = -1;
		// int before string! 0xff is recognized as string otherwise
		if (lua_isinteger(L,4))
			fgc = lua_tointeger(L,4);
		else if (lua_isstring(L,4))
			fgc = md->getColor(color_get(lua_tostring(L,4)));
		int32_t bgc = -1;
		if (lua_isinteger(L,5))
			bgc = lua_tointeger(L,5);
		else if (lua_isstring(L,5))
			bgc = md->getColor(color_get(lua_tostring(L,5)));
		md->drawText(x,y,text,-1,fgc,bgc);
		return 0;
	}
	lua_pushliteral(L,"fb_drawtext: no display.");
	lua_error(L);
	return 0;
}


static int luax_fb_fillrect(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		int x = luaL_checkinteger(L,1);
		int y = luaL_checkinteger(L,2);
		int w = luaL_checkinteger(L,3);
		int h = luaL_checkinteger(L,4);
		int32_t col = -1;
		if (lua_isinteger(L,5))
			col = lua_tointeger(L,5);
		else if (lua_isstring(L,5))
			col = md->getColor(color_get(lua_tostring(L,5)));
		md->fillRect(x,y,w,h,col);
		return 0;
	}
	lua_pushliteral(L,"fb_drawrect: no display.");
	lua_error(L);
	return 0;
}


static const LuaFn FbFunctions[] = {
	{ "fb_drawrect", luax_fb_drawrect, "draw rectangle" },
	{ "fb_drawtext", luax_fb_drawtext, "draw text string" },
	{ "fb_drawline", luax_fb_drawline, "draw line" },
	{ "fb_drawhline", luax_fb_drawhline, "draw horizontal line" },
	{ "fb_drawvline", luax_fb_drawvline, "draw vertical line" },
	{ "fb_fillrect", luax_fb_fillrect, "fill rectangle" },
	{ "fb_setbgcol", luax_fb_setbgcol, "set background color" },
	{ "fb_setfgcol", luax_fb_setfgcol, "set foreground color" },
	{ "fb_setfont", luax_fb_setfont, "set font" },
	{ 0, 0, 0 },
};


#endif


void screen_setup()
{
	TextDisplay *d = TextDisplay::getFirst();
	if (d == 0)
		return;
	Ctx = new Screen;
	Ctx->disp = d;
	Ctx->digits = d->charsPerLine();
	Ctx->modestart = uptime();
	d->setCursor(false);
	d->setBlink(false);
	action_add("display!set_mode",switch_mode,0,"switch to next or specified display mode");
	if (d->maxBrightness() > 1) {
		action_add("display!set_bright",bright_set,0,"set brightness to argument value");
	}
	action_add("display!off",shutdown,Ctx,"clock: turn off");
	action_add("display!on",poweron,Ctx,"clock: turn on");
	action_add("sw!startstop",sw_startstop,Ctx,"stopwatch start/stop");
	action_add("sw!reset",sw_clear,Ctx,"stopwatch reset");
	action_add("sw!pause",sw_pause,Ctx,"stopwatch pause/resume");
	shutdown(Ctx);
	poweron(Ctx);
	d->setBrightness(d->maxBrightness());
	Ctx->mode = cm_version;
	Ctx->modech = true;
	d->clear();
	cyclic_add_task("display", clock_iter, Ctx);
#ifdef CONFIG_LUA
	xlua_add_funcs("screen",Functions);	// TODO display mode Lua is missing
	if (Ctx->disp->toMatrixDisplay())
		xlua_add_funcs("fb",FbFunctions);	// TODO display mode Lua is missing
#endif
	log_info(TAG,"ready");
}

#endif
