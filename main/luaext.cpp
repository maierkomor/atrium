/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#ifdef CONFIG_LUA

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include "actions.h"
#include "env.h"
#include "globals.h"
#include "log.h"
#include "luaext.h"
#include "profiling.h"
#include "romfs.h"
#include "screen.h"
#include "shell.h"
#include "strstream.h"
#include "swcfg.h"
#include "terminal.h"
#include "timefuse.h"

#include <set>

#define TAG MODULE_LUA

#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
#define HAVE_FS
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef ESP8266
#define USE_FOPEN
#endif

using namespace std;

struct LuaFns {
	LuaFns(const char *m, const LuaFn *f)
	: module(m)
	, fns(f)
	, next(List)
	{
		List = this;
	}

	const char *module;
	const LuaFn *fns;
	LuaFns *next;
	static LuaFns *List;
};

static SemaphoreHandle_t Mtx = 0;
static lua_State *LS = 0;
static set<string> Compiled;
static void xlua_init();

LuaFns *LuaFns::List = 0;


static int f_event_create(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	const char *a = strchr(n,'`');
	if (a == 0) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	int e = event_register(n);
	if (e == 0) {
		lua_pushliteral(L,"Error registering event.");
		lua_error(L);
	}
	lua_pushinteger(L,e);
	return 1;
}


static int f_event_trigger(lua_State *L)
{
	const char *arg = 0;
	if (lua_type(L,2) == LUA_TSTRING) {
		arg = strdup(lua_tostring(L,2));
	}
	switch (lua_type(L,1)) {
	case LUA_TSTRING:
		if (arg)
			event_trigger_arg(event_id(lua_tostring(L,1)),(void*)arg);
		else
			event_trigger(event_id(lua_tostring(L,1)));
		break;
	case LUA_TNUMBER:
		if (arg)
			event_trigger_arg((event_t)lua_tonumber(L,1),(void*)arg);
		else
			event_trigger((event_t)lua_tonumber(L,1));
		break;
	default:
		lua_pushliteral(L,"invalid argument");
		lua_error(L);
	}
	return 0;
}


static int f_action_activate(lua_State *L)
{
	const char *an = luaL_checkstring(L,1);
	const char *arg = 0;
	Action *a = action_get(an);
	if (a == 0) {
		lua_pushfstring(L,"unknwon action '%s'",an);
		lua_error(L);
	}
	if (lua_type(L,2) == LUA_TSTRING) {
		arg = strdup(lua_tostring(L,2));
	}
	a->activate((void*)arg);
	return 0;
}


int f_warn(lua_State *L)
{
	const char *s = luaL_checkstring(L,1);
	lua_pop(L,1);
	log_direct(ll_warn,TAG,"%s",s);
	return 0;
}


int f_info(lua_State *L)
{
	const char *s = luaL_checkstring(L,1);
	lua_pop(L,1);
	log_direct(ll_info,TAG,"%s",s);
	return 0;
}


int f_dbug(lua_State *L)
{
	const char *s = luaL_checkstring(L,1);
	lua_pop(L,1);
	log_direct(ll_debug,TAG,"%s",s);
	return 0;
}


int f_uptime(lua_State *L)
{
	lua_pushnumber(L,(float)timestamp()*1E-6);
	return 1;
}


int f_timestamp(lua_State *L)
{
	lua_pushinteger(L,timestamp()&UINT32_MAX);
	return 1;
}


static int f_setvar(lua_State *L)
{
	const char *var = luaL_checkstring(L,1);
	if (0 == var) {
		lua_pushliteral(L,"setvar: arg0 must be a string");
	} else if (EnvElement *e = RTData->getChild(var)) {
		if (EnvString *s = e->toString()) {
			if (const char *str = lua_tostring(L,2)) {
				s->set(str);
				return 0;
			}
			lua_pushliteral(L,"invalid argument type");
		} else if (EnvNumber *n = e->toNumber()) {
			n->set(luaL_checknumber(L,2));
			return 0;
		} else if (EnvBool *b = e->toBool()) {
			if (lua_isboolean(L,2)) {
				b->set(lua_toboolean(L,2));
				return 0;
			}
			lua_pushliteral(L,"invalid argument type");
		} else {
			lua_pushliteral(L,"unsupported argument");
		}
	} else {
		lua_pushfstring(L,"setvar: variable %s not found",var);
	}
	lua_error(L);
	return 0;
}


static int f_newvar(lua_State *L)
{
	const char *var = luaL_checkstring(L,1);
	if (RTData->getChild(var)) {
		lua_pushfstring(L,"newvar('%s'): variable already exists",var);
		lua_error(L);
	}
	switch (lua_type(L,2)) {
	case LUA_TSTRING:
		RTData->add(new EnvString(var,lua_tostring(L,2)));
		break;
	case LUA_TBOOLEAN:
		RTData->add(new EnvBool(var,lua_toboolean(L,2)));
		break;
	case LUA_TNUMBER:
		{
			EnvNumber *n = new EnvNumber(var,lua_tonumber(L,2));
			RTData->add(n);
			if (lua_isstring(L,3))
				n->setDimension(strdup(lua_tostring(L,3)));
		}
		break;
	default:
		log_warn(TAG,"newvar: unexptected type %d",lua_type(L,2));
		abort();
	}
	return 0;
}


static int f_getvar(lua_State *L)
{
	const char *var = luaL_checkstring(L,1);
	EnvElement *e = RTData->getChild(var);
	lua_pop(L,1);
	if (0 == e) {
		lua_pushnil(L);
		return 1;
	}
	log_dbug(TAG,"getvar('%s')",var);
	if (EnvString *s = e->toString()) {
		lua_pushstring(L,s->get());
	} else if (EnvNumber *n = e->toNumber()) {
		lua_pushnumber(L,(float)n->get());
	} else if (EnvBool *b = e->toBool()) {
		lua_pushboolean(L,b->get());
	} else {
		estring str;
		strstream ss(str);
		e->toStream(ss);
		lua_pushstring(L,str.c_str());
	}
	return 1;
}


static int f_print(lua_State *L)
{
	const char *str = lua_tostring(L,1);
	if (str) {
		log_dbug(TAG,"con_print '%s'",str);
		con_print(str);
	}
	lua_pop(L,1);
	return 0;
}


static int f_random(lua_State *L)
{
	lua_pushinteger(L,random());
	return 1;
}


static int f_tmr_create(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	int iv = luaL_checkinteger(L,2);
	if (iv < 10) {
		lua_pushliteral(L,"Invalid argument #2.");
		lua_error(L);
	}
	if (timefuse_create(n,iv,false) == 0) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	return 0;
}


static int f_tmr_start(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	if (timefuse_start(n)) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	return 0;
}


static int f_tmr_stop(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	if (timefuse_stop(n)) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	return 0;
}


int luax_disp_max_x(lua_State *L);
int luax_disp_max_y(lua_State *L);
int luax_disp_draw_rect(lua_State *L);
int luax_disp_set_cursor(lua_State *L);
int luax_disp_set_font(lua_State *L);
int luax_disp_write(lua_State *L);
int luax_rgbleds_get(lua_State *L);
int luax_rgbleds_set(lua_State *L);
int luax_rgbleds_write(lua_State *L);
int luax_tlc5947_get(lua_State *L);
int luax_tlc5947_set(lua_State *L);
int luax_tlc5947_write(lua_State *L);

static const LuaFn CoreFunctions[] = {
	{ "getvar", f_getvar, "get variable visible via env command" },
	{ "setvar", f_setvar, "set variable visible via env command" },
	{ "newvar", f_newvar, "create variable visible via env command" },
	{ "con_print", f_print, "print to console" },
	// math.random yields a float....
	{ "random", f_random, "create a 32-bit integer random number" },
	{ "action_activate", f_action_activate, "activate an action (action[,arg])" },
	{ "event_trigger", f_event_trigger, "trigger an event (event[,arg])" },
	{ "event_create", f_event_create, "create an event (event) = <int>" },
	{ "log_warn", f_warn, "write warning to log" },
	{ "log_info", f_info, "write information to log" },
	{ "log_dbug", f_dbug, "write debug message to log" },
	{ "uptime", f_uptime, "returns uptime as float in seconds" },
	{ "timestamp", f_timestamp, "returns timestamp as 32-bit us (will wrap)" },
	{ "tmr_create", f_tmr_create, "creates a timer" },
	{ "tmr_start", f_tmr_start, "start a timer" },
	{ "tmr_stop", f_tmr_stop, "stop a timer" },
	{ 0, 0, 0 },
};


int xlua_add_funcs(const char *name, const LuaFn *f)
{
	new LuaFns(name,f);
	if (LS == 0)
		return 0;
	while (f->name) {
		lua_pushcfunction(LS,f->func);
		lua_setglobal(LS,f->name);
		++f;
	}
	return 0;
}


static int xlua_parse_file(const char *fn, const char *n = 0)
{
	int fd;
	if (n == 0)
		n = fn;
	char vn[strlen(n)+1];
	if (const char *sl = strrchr(n,'/'))
		strcpy(vn,sl+1);
	else
		strcpy(vn,n);
	if (char *d = strchr(vn,'.'))
		*d = 0;
#ifdef CONFIG_ROMFS
	fd = romfs_open(fn);
	if (fd != -1) {
		size_t s = romfs_size_fd(fd);
#ifdef CONFIG_IDF_TARGET_ESP32
		const char *buf = (const char *) romfs_mmap(fd);
#else
		char *buf = (char *) malloc(s);
		romfs_read_at(fd,buf,s,0);
#endif
		Lock lock(Mtx);
		if (LS == 0)
			xlua_init();
		int r;
		if (0 == luaL_loadbuffer(LS,buf,s,vn)) {
			lua_setglobal(LS,vn);
			Compiled.insert(vn);
			r = 0;
		} else {
			r = 1;
			log_warn(TAG,"parse %s: %s",fn,lua_tostring(LS,-1));
			lua_pop(LS,1);
		}
#ifndef CONFIG_IDF_TARGET_ESP32
		free(buf);
#endif
		return r;
	}
#endif
#ifdef HAVE_FS
#ifdef USE_FOPEN
	// TODO: esp8266 must use fopen, but has too little RAM anyway...
	/*
	   if (FILE *f = fopen(fn,O_RDONLY)) {
	   }
	   */
#else
	fd = open(fn,O_RDONLY);
	if (fd != -1) {
		struct stat st;
		if (0 == fstat(fd,&st)) {
			char *buf = (char *) malloc(st.st_size);
			int n = pread(fd,buf,st.st_size,0);
			close(fd);
			if (st.st_size == n) {
				Lock lock(Mtx);
				if (LS == 0)
					xlua_init();
				if (0 == luaL_loadbuffer(LS,buf,st.st_size,vn)) {
					lua_setglobal(LS,vn);
					Compiled.insert(vn);
					return 0;
				}
				log_warn(TAG,"parse %s: %s",fn,lua_tostring(LS,-1));
				lua_pop(LS,1);
			} else {
				log_warn(TAG,"parse %s: read error",fn);
			}
		} else {
			close(fd);
			log_warn(TAG,"parse %s: stat error",fn);
		}
		return 1;
	}
#endif // USE_FOPEN
#endif
	return 1;
}


const char *xluac(Terminal &t, int argc, const char *args[])
{
	if (LS == 0)
		xlua_init();
	if ((argc == 1) || (0 == strcmp(args[1],"-h")))
		return help_cmd(t,args[0]);
	if (0 == strcmp(args[1],"-i")) {
		LuaFns *fn = LuaFns::List;
		while (fn) {
			t.printf("%s functions:\n",fn->module);
			const LuaFn *f = fn->fns;
			while (f->name) {
				t.printf("%-16s: %s\n",f->name,f->descr);
				++f;
			}
			fn = fn->next;
		}
		return 0;
	}
	if (0 == strcmp(args[1],"-l")) {
		for (auto &x : Compiled)
			t.println(x.c_str());
		return 0;
	}
	if (argc > 3)
		return "Invalid number of arguments.";
	int fd;
	const char *n = argc == 2 ? args[1] : args[2];
	char vn[strlen(n)+1];
	if (const char *sl = strrchr(n,'/'))
		strcpy(vn,sl+1);
	else
		strcpy(vn,n);
	if (char *d = strchr(vn,'.'))
		*d = 0;
#ifdef CONFIG_ROMFS
	fd = romfs_open(args[1]);
	if (fd != -1) {
		size_t s = romfs_size_fd(fd);
#ifdef CONFIG_IDF_TARGET_ESP32
		const char *buf = (const char *) romfs_mmap(fd);
#else
		char *buf = (char *) malloc(s);
		romfs_read_at(fd,buf,s,0);
#endif
		Lock lock(Mtx);
		const char *r;
		if (0 == luaL_loadbuffer(LS,buf,s,vn)) {
			lua_setglobal(LS,vn);
			Compiled.insert(vn);
			r = 0;
		} else {
			t.println(lua_tostring(LS,-1));
			lua_pop(LS,1);
			r = "Failed.";
		}
#ifndef CONFIG_IDF_TARGET_ESP32
		free(buf);
#endif
		return r;
	}
#endif
#ifdef HAVE_FS
	const char *fn = args[1];
	const estring &pwd = t.getPwd();
	size_t pl = pwd.size();
	size_t fl = strlen(fn) + 1;
	char fp[pl+fl+1];
	fp[0] = 0;
	if (fn[0] != '/') {
		memcpy(fp,pwd.data(),pl);
		fp[pl] = '/';
		memcpy(fp+pl+1,fn,fl);
	} else 
		memcpy(fp,fn,fl);
	fd = open(fp,O_RDONLY);
	if (fd != -1) {
		struct stat st;
		const char *err = 0;
		if (0 == fstat(fd,&st)) {
			char *buf = (char *) malloc(st.st_size);
			int n = pread(fd,buf,st.st_size,0);
			close(fd);
			if (st.st_size == n) {
				Lock lock(Mtx);
				if (0 == luaL_loadbuffer(LS,buf,st.st_size,vn)) {
					lua_setglobal(LS,vn);
					Compiled.insert(vn);
					return 0;
				}
				t.println(lua_tostring(LS,-1));
				lua_pop(LS,1);
				return "";
			} else {
				err = "read error";
			}
		} else {
			close(fd);
			err = "cannot stat file";
		}
		t.println(err);
		return err;
	} else {
		log_warn(TAG,"open %s: %s",args[1],strerror(errno));
	}
#endif
	return "Invalid argument #1.";
}


unsigned xlua_render(Screen *ctx)
{
	unsigned d = 50;
	if (lua_getglobal(LS,"render_screen")) {
		if (0 != lua_pcall(LS,0,1,0)) {
			log_dbug(TAG,"lua render_screen: exe error");
			if (const char *str = lua_tostring(LS,-1))
				log_warn(TAG,"render_screen: %s",str);
			ctx->mode = (clockmode_t) (ctx->mode + 1);
		} else if (int x = lua_tointeger(LS,-1)) {
			if (x > 10)
				d = x;
		}
		lua_settop(LS,0);
		lua_gc(LS,LUA_GCCOLLECT);
	} else {
		ctx->mode = (clockmode_t) (ctx->mode + 1);
	}
	return d;
}


const char *xlua_exe(Terminal &t, const char *script)
{
	if (LS == 0)
		xlua_init();
	Lock lock(Mtx);
	PROFILE_FUNCTION();
	log_dbug(TAG,"execute '%s'",script);
	const char *r = 0;
	if (lua_getglobal(LS,script)) {
		log_dbug(TAG,"found global %s",script);
	} else if (0 == luaL_loadbuffer(LS,script,strlen(script),"cmdline")) {
	} else {
		r = "Parser error.";
	}
	if ((r == 0) && (0 != lua_pcall(LS,0,1,0))) {
		r = "Execution error.";
	}
	if (const char *str = lua_tostring(LS,-1)) {
		t.println(str);
		r = "";
	}
	lua_settop(LS,0);
	lua_gc(LS,LUA_GCCOLLECT);
	return r;
}


static void xlua_script(void *arg)
{
	if (arg == 0)
		return;
	Lock lock(Mtx);
	if (LS == 0)
		xlua_init();
	PROFILE_FUNCTION();
	const char *script = (const char *) arg;
	log_dbug(TAG,"lua!run '%s'",script);
	bool run = true;
	if (lua_getglobal(LS,script)) {
		log_dbug(TAG,"found global %s",script);
	} else if (0 == luaL_loadbuffer(LS,script,strlen(script),"lua!run")) {
	} else {
		run = false;
		log_warn(TAG,"lua!run '%s': parser error",script);
	}
	if (run && (0 != lua_pcall(LS,0,1,0)))
		log_warn(TAG,"lua!run '%s': exe error",script);
	if (const char *str = lua_tostring(LS,-1)) {
		log_warn(TAG,"lua!run '%s': %s",script,str);
		lua_pop(LS,1);
	}
	lua_settop(LS,0);
	lua_gc(LS,LUA_GCCOLLECT);
	free(arg);
}


static void xlua_init()
{
	if (LS == 0) {
		LS = luaL_newstate();
		luaL_requiref(LS,"base",luaopen_base,1);
		lua_pop(LS,1);
		luaL_requiref(LS,"math",luaopen_math,1);
		lua_pop(LS,1);
		luaL_requiref(LS,"string",luaopen_string,1);
		lua_pop(LS,1);
		luaL_requiref(LS,"table",luaopen_table,1);
		lua_pop(LS,1);
		xlua_add_funcs("core",CoreFunctions);
		LuaFns *l = LuaFns::List;
		while (l) {
			const LuaFn *f = l->fns;
			while (f->name) {
				lua_pushcfunction(LS,f->func);
				lua_setglobal(LS,f->name);
				++f;
			}
			l = l->next;
		}
	}
}


void xlua_setup()
{
	Mtx = xSemaphoreCreateMutex();
	action_add("lua!run",xlua_script,0,"run argument as Lua script");
	for (const auto &fn : Config.luafiles())
		xlua_parse_file(fn.c_str());
}

#endif
