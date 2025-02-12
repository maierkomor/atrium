/*
 *  Copyright (C) 2018-2024, Thomas Maier-Komor
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
#include "log.h"
#include "luaext.h"
#include "mstream.h"
#include "profiling.h"
#include "screen.h"
#include "swcfg.h"

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

static const char *Modes[] = {
	"splash screen",
	"local time",
	"date",
	"stop watch",
#ifdef CONFIG_LUA
	"Lua",
#endif
};

#define CLOCK_MODE_MAX (sizeof(Modes)/sizeof(Modes[0]))

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


static void mode_name(clockmode_t &x, char *mode)
{
	if (x < CLOCK_MODE_MAX) {
		strcpy(mode,Modes[x]);
	} else if (EnvElement *e = RTData->getElement(x-CLOCK_MODE_MAX)) {
		const char *n = e->name();
		log_dbug(TAG,"getElement %d: %s",x,n);
		mode[0] = 0;
		if (const char *pn = e->getParent()->name()) {
			strcpy(mode,pn);
			strcat(mode,"/");
		}
		strcat(mode,n);
	} else {
		x = cm_version;
		strcpy(mode,Modes[0]);
	}
	log_dbug(TAG,"mode_name %s",mode);
}


static bool mode_enabled(const char *mode)
{
#ifdef CONFIG_LUA
	if ((0 == strcmp(mode,"Lua")) && Config.lua_disable())
		return false;
#endif
	const auto &envs = Config.screen().envs();
	if (envs.empty())
		return true;
	for (const auto &e : envs) {
		if (0 == strcmp(mode,e.path().c_str())) {
			log_dbug(TAG,"enabled mode %s",mode);
			return true;
		}
	}
	log_dbug(TAG,"DISABLED mode %s",mode);
	return false;
}


static inline const char *env_title(const char *path)
{
	const auto &envs = Config.screen().envs();
#if 0	// compiler bug on ESP32-S3
	for (auto e : envs) {
#else
	size_t n = envs.size();
	for (int i = 0; i < n; ++i) {
		const auto &e = envs[i];
#endif
		if (0 == strcmp(path,e.path().c_str())) {
			const auto &t = e.title();
			if (!t.empty())
				path = t.c_str();
			break;
		}
	}
	return path;

}


static void mode_next(const char *m)
{
	clockmode_t mode = Ctx->mode;
	char modename[64];
	do {
		if (m == 0) {
			mode = (clockmode_t)((int)mode + 1);
		} else if (0 == strcmp("time",m)) {
			mode = cm_time;
		} else if (0 == strcmp("local time",m)) {
			mode = cm_time;
		} else if (0 == strcmp("date",m)) {
			mode = cm_date;
		} else if (0 == strcmp("stopwatch",m)) {
			mode = cm_stopwatch;
#ifdef CONFIG_LUA
		} else if ((0 == strcasecmp("lua",m)) && !Config.lua_disable()) {
			mode = cm_lua;
#endif
		} else {
			int idx = RTData->getIndex(m);
			if (idx == -1) {
				log_warn(TAG,"invalid mode request %s",m);
				return;
			}
			idx += CLOCK_MODE_MAX;
			log_dbug(TAG,"index %d",idx);
			mode = (clockmode_t) (idx);
		}
		if (mode >= CLOCK_MODE_MAX+RTData->numElements())
			mode = cm_version;
		mode_name(mode,modename);
		m = 0;
	} while (!mode_enabled(modename));
	Ctx->mode = mode;
	Ctx->path = modename;
	Ctx->modestart = uptime();
	Ctx->modech = true;
}


static void switch_mode(void *arg)
{
	const char *m = (const char *) arg;
	mode_next(m);
	/*
	if (0 == m) {
		while (0 == mode_enabled(Ctx->path.c_str()))
			mode_next(0);
	}
	*/
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


void Screen::exeEnvAction(void *arg)
{
	Screen *Ctx = (Screen *) arg;
	const auto &envs = Config.screen().envs();
	if (envs.empty())
		return;
	char modename[64];
	mode_name(Ctx->mode,modename);
	for (const auto &e : envs) {
		if (strcmp(modename,e.path().c_str()))
			continue;
		log_dbug(TAG,"action of mode %s",modename);
		const char *ac = e.action().c_str();
		if (const char *s = strchr(ac,' ')) {
			char name[s-ac+1];
			memcpy(name,ac,s-ac);
			name[s-ac] = 0;
			action_activate_arg(name,(void *)(s+1));
		} else {
			action_activate(ac);
		}
		return;
	}
}


void Screen::display_value(const char *str)
{
	if (strcmp(str,prev.c_str())) {
		if (MatrixDisplay *dm = disp->toMatrixDisplay()) {
			uint16_t maxx = dm->maxX()-6;
			int f = font_large;
			const Font *font;
			unsigned tw;
			do {
				font = dm->getFont(f);
				tw = font->textWidth(str);
				++f;
			} while ((tw > maxx) && (0 != f));
			unsigned fh = font->maxY-font->minY;
			dm->setupOffScreen(5,ypos,lw>tw?lw:tw,fh,0);
			const Font *of = dm->getFont();
			dm->setFont(font);
			dm->drawText(5,ypos,str,-1,0xffff,-2);
			dm->setFont(of);
			dm->commitOffScreen();
			lw = tw;
		} else {
			disp->write(str);
		}
		prev = str;
	}
}


void Screen::display_time()
{
	uint8_t h=0,m=0,s=0;
	unsigned y=0;
	const char *str;
	if (get_time_of_day(&h,&m,&s,0,0,0,&y)) {
		str = "--:--:--";
	} else {
		char *buf = (char *) alloca(16);
		sprintf(buf,"%02u:%02u:%02u",h,m,s);
		str = buf;
	}
	display_value(str);
}


void Screen::display_date()
{
	uint8_t wd=0,day=0,mon=0;
	unsigned year=0;
	const char *str;
	if (get_time_of_day(0,0,0,&wd,&day,&mon,&year)) {
		str = "--.--.----";
	} else {
		char *buf = (char *) alloca(16);
		sprintf(buf,"%u.%u.%u",day,mon,year);
		str = buf;
	}
	display_value(str);
}


void Screen::display_env()
{
	PROFILE_FUNCTION();
	EnvElement *e = RTData->getElement(Ctx->mode-CLOCK_MODE_MAX);
	if (e == 0)
		return;
	const char *str = 0;
	if (EnvNumber *n = e->toNumber()) {
		char *buf = (char *)alloca(128);
		size_t a;
		const char *fmt = n->getFormat();
		double v = n->get();
		if (!isnan(v) && (fmt != 0)) {
			a = snprintf(buf,128,fmt,v);
			str = buf;
		} else {
			if (Ctx->disp->hasAlpha())
				str = "NaN";
			else
				str = "---";
			a = 3;
		}
		if (const char *dim = e->getDimension()) {
			buf[a++] = ' ';
			strcpy(buf+a,dim);
		}
	} else if (EnvBool *b = e->toBool()) {
		str = b->get() ? "on" : "off";
	} else if (EnvString *s = e->toString()) {
		str = s->get();
	} else {
		char *buf = (char*)alloca(128);
		mstream o(buf,128);
		e->writeValue(o);
		str = o.c_str();	// to add terminating \0
	}
	display_value(str);
}


void Screen::display_version()
{
	const char *sp = strchr(Version,' ');
	if (MatrixDisplay *dm = disp->toMatrixDisplay()) {
		uint16_t maxx = dm->maxX();
		uint16_t maxy = dm->maxY();
		dm->drawRect(4,1,maxx-5,maxy-3);
		const Font *f = dm->setFont(font_large);
		ypos = 6;
		dm->setupOffScreen(11,ypos,100,f->maxY-f->minY,0);
		dm->drawText(11,ypos,"Atrium",6,-1,-2);
		dm->commitOffScreen();
		ypos += f->yAdvance;
		f = dm->setFont(font_medium);
		if (ypos + 2*f->yAdvance > maxy) {
			f = dm->setFont(font_small);
			if (ypos + 2*f->yAdvance > maxy) {
				f = dm->setFont(font_tiny);
			}
		}
		uint16_t tw = dm->textWidth(Version);
		int l;
		if (tw < (maxx-19)) {
			l = -1;
		} else {
			l = sp-Version;
			tw = dm->textWidth(Version,l);
		}
		dm->setupOffScreen(11,ypos,tw,f->maxY-f->minY,0);
		dm->drawText(11,ypos,Version,l);
		dm->commitOffScreen();
		ypos += f->yAdvance;
		if (maxy >= ypos+f->yAdvance) {
			uint16_t tw = dm->textWidth(Copyright);
			if (tw > maxx) {
				f = dm->setFont(font_tiny);
				tw = dm->textWidth(Copyright);
			}
			dm->setupOffScreen(11,ypos,tw,f->maxY-f->minY,0);
			dm->drawText(11,ypos,Copyright,29,0xffff,-2);
			dm->commitOffScreen();
		}
	} else {
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
	if ((h != 0) && (digits >= 8)) {
		sprintf(str,"%02u:%02u:%02u.%u",h,m,s,dsec);
	} else if (digits >= 6) {
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
	if (strcmp(str,prev.c_str())) {
		if (MatrixDisplay *dm = disp->toMatrixDisplay()) {
			// monospace font - i.e. all glyphs have same width
			const Font *f = dm->getFont();
			unsigned tw = f->getGlyph(':')->xAdvance * 3 + f->getGlyph('8')->xAdvance * 7;
			dm->setupOffScreen(5,ypos,tw,f->maxY-f->minY,0);
			dm->drawText(5,ypos,str);
			dm->commitOffScreen();
		} else {
			Ctx->disp->write("\r");
			disp->write(str);
		}
		prev = str;
	}
}


static void print_title()
{
	const char *text = env_title(Ctx->path.c_str());
	log_dbug(TAG,"mode %s",text);
	if (MatrixDisplay *dm = Ctx->disp->toMatrixDisplay()) {
		const Font *f = dm->setFont(font_medium);
		unsigned dy = 0;
		unsigned tw = dm->textWidth(text);
		if ((tw + 4) >= dm->maxX()) {
			uint16_t b0 = f->blOff;
			f = dm->setFont(font_small);
			tw = dm->textWidth(text);
			if ((tw + 4) >= dm->maxX())
				f = dm->setFont(font_tiny);
			dy = b0 - f->blOff;
		}
		dm->setupOffScreen(5,4+dy,tw,f->maxY-f->minY,0);
		dm->drawText(5,4+dy,text);
		dm->commitOffScreen();
		log_dbug(TAG,"header %s",text);
		Ctx->ypos = 5 + dy + f->yAdvance;
		dm->setFont(font_large);
	} else {
		Ctx->disp->setPos(0,0);
		Ctx->disp->write(text);
	}
}


static unsigned clock_iter(void *arg)
{
	bool alpha = Ctx->disp->hasAlpha();
	Screen *Ctx = (Screen *)arg;
	MatrixDisplay *dm = Ctx->disp->toMatrixDisplay();
	if (Ctx->modech) {
		Ctx->disp->clear();
		Ctx->modech = false;
		Ctx->prev.clear();
		if (alpha) {
			if (cm_version == Ctx->mode) {
				Ctx->display_version();
				Ctx->disp->flush();
				return 50;
			}
			print_title();
		}
	}
	unsigned d = 100;
	if (0 == dm) {
		Ctx->disp->write("\r");
	}
	switch (Ctx->mode) {
	case cm_date:
		Ctx->display_date();
		d = 100;
		break;
	case cm_time:
		Ctx->display_time();
		break;
	case cm_version:
		{
			uint32_t ut = uptime();
			time_t now;
			time(&now);
			if ((now > 3600*24*365*30) && (ut - Ctx->modestart > 4000)) {
				mode_next(Modes[1]);
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
		if (!Config.lua_disable()) {
			d = xlua_render(Ctx);
		} else
#else
		{
			Ctx->mode = (clockmode_t)((int)Ctx->mode + 1);
			Ctx->modech = true;
		}
#endif
		break;
	default:
		Ctx->display_env();
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
	lua_pushstring(L,"No display.");
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
	lua_pushstring(L,"No display.");
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
			if (0 != md->setFont(fn))
				return 0;
	lua_pushstring(L,"Invalid font.");
	lua_error(L);
	return 0;
}


int luax_disp_set_mode(lua_State *L)
{
	const char *m = lua_tostring(L,1);
	mode_next(m);
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
	{ "disp_set_mode", luax_disp_set_mode, "set mode (mode or empty for next)" },
	{ "disp_write", luax_disp_write, "write text at cursor position with font" },
	{ "disp_clear", luax_disp_clear , "clear the screen" },
	{ 0, 0, 0 }
};


int luax_fb_drawicon(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		int x = luaL_checkinteger(L,1);
		int y = luaL_checkinteger(L,2);
		const char *fn = luaL_checkstring(L,3);
		int col = 0;
		if (lua_isstring(L,4))
			col = color_get(lua_tostring(L,4));
		else if (lua_isinteger(L,4))
			col = lua_tointeger(L,4);
		md->drawIcon(x,y,fn,col);
		return 0;
	}
	lua_pushliteral(L,"No display.");
	lua_error(L);
	return 0;
}


static int luax_fb_setbgcol(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		if (lua_isinteger(L,1))
			md->setBgColorVal(lua_tointeger(L,1));
		else if (lua_isstring(L,1))
			md->setBgColor(color_get(lua_tostring(L,1)));
		return 0;
	}
	lua_pushliteral(L,"No display.");
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
	lua_pushliteral(L,"No display.");
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
	lua_pushliteral(L,"No display.");
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
	lua_pushliteral(L,"No display.");
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
	lua_pushliteral(L,"No display.");
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
	lua_pushliteral(L,"No display.");
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
	lua_pushliteral(L,"No display.");
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
	lua_pushliteral(L,"No display.");
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
	lua_pushliteral(L,"No display.");
	lua_error(L);
	return 0;
}


static int luax_fb_flush(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		md->flush();
		return 0;
	}
	lua_pushliteral(L,"No display.");
	lua_error(L);
	return 0;
}


static int luax_fb_offscreen(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		int x = luaL_checkinteger(L,1);
		int y = luaL_checkinteger(L,2);
		int w = luaL_checkinteger(L,3);
		int h = luaL_checkinteger(L,4);
		int bg = -1;
		if (lua_isinteger(L,5))
			bg = lua_tointeger(L,5);
		md->setupOffScreen(x,y,w,h,bg);
		return 0;
	}
	lua_pushliteral(L,"No display.");
	lua_error(L);
	return 0;
}


static int luax_fb_commit(lua_State *L)
{
	if (MatrixDisplay *md = Ctx->disp->toMatrixDisplay()) {
		md->commitOffScreen();
		return 0;
	}
	lua_pushliteral(L,"No display.");
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
	{ "fb_setfont", luax_fb_setfont, "set font (fontname)" },
	{ "fb_drawicon", luax_fb_drawicon, "draw icon (x,y,file[,color])" },
	{ "fb_flush", luax_fb_flush, "flush to display" },
	{ "fb_offscreen", luax_fb_offscreen, "setup off-screen area" },
	{ "fb_commit", luax_fb_commit, "commit off-screen area" },
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
	action_add("display!off",shutdown,Ctx,"turn display off");
	action_add("display!on",poweron,Ctx,"turn display on");
	action_add("sw!startstop",sw_startstop,Ctx,"stopwatch start/stop");
	action_add("sw!reset",sw_clear,Ctx,"stopwatch reset");
	action_add("sw!pause",sw_pause,Ctx,"stopwatch pause/resume");
	action_add("screen!action",Screen::exeEnvAction,Ctx,"activate screen mode specific action");
	shutdown(Ctx);
	poweron(Ctx);
	d->setBrightness(d->maxBrightness());
	Ctx->mode = cm_version;
	Ctx->modech = true;
	d->clear();
	cyclic_add_task("display", clock_iter, Ctx);
#ifdef CONFIG_LUA
	if (!Config.lua_disable()) {
		xlua_add_funcs("screen",Functions);
		if (d->toMatrixDisplay())
			xlua_add_funcs("fb",FbFunctions);
	}
#endif
	log_info(TAG,"ready");
}

#endif
