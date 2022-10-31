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
#include "romfs.h"
#include "shell.h"
#include "strstream.h"
#include "swcfg.h"
#include "terminal.h"

#include <vector>

#define TAG MODULE_LUA

#ifdef ESP8266
#define USE_FOPEN
#endif

using namespace std;

static SemaphoreHandle_t Mtx = 0;
static lua_State *LS = 0;
static vector<string> Compiled;
static void xlua_init();


static int f_sin(lua_State *L)
{
	float f = luaL_checknumber(L,1);
	lua_pop(L,1);
	lua_pushnumber(L,sinf(f));
	return 1;
}


static int f_cos(lua_State *L)
{
	float f = luaL_checknumber(L,1);
	lua_pop(L,1);
	lua_pushnumber(L,cosf(f));
	return 1;
}


static int f_tan(lua_State *L)
{
	float f = luaL_checknumber(L,1);
	lua_pop(L,1);
	lua_pushnumber(L,tanf(f));
	return 1;
}


static int f_sqrt(lua_State *L)
{
	float f = luaL_checknumber(L,1);
	lua_pop(L,1);
	lua_pushnumber(L,sqrtf(f));
	return 1;
}


static int f_log(lua_State *L)
{
	float f = luaL_checknumber(L,1);
	lua_pop(L,1);
	lua_pushnumber(L,logf(f));
	return 1;
}


static int f_event_trigger(lua_State *L)
{
	const char *arg = 0;
	if (lua_type(L,2) == LUA_TSTRING) {
		arg = lua_tostring(L,2);
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
	if (lua_type(L,2) == LUA_TSTRING) {
		arg = lua_tostring(L,2);
	}
	Action *a = action_get(an);
	lua_pop(L,1);
	if (a == 0) {
		lua_pushfstring(L,"unknwon action '%s'",an);
		lua_error(L);
	}
	a->activate((void*)arg);
	if (arg)
		lua_pop(L,1);
	return 0;
}


int f_warn(lua_State *L)
{
	const char *s = luaL_checkstring(L,1);
	log_direct(ll_warn,TAG,"%s",s);
	return 0;
}


int f_info(lua_State *L)
{
	const char *s = luaL_checkstring(L,1);
	log_direct(ll_info,TAG,"%s",s);
	return 0;
}


int f_dbug(lua_State *L)
{
	const char *s = luaL_checkstring(L,1);
	log_direct(ll_debug,TAG,"%s",s);
	return 0;
}


int f_uptime(lua_State *L)
{
	lua_pushnumber(L,(float)timestamp()*1E-6);
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


static const luaL_Reg Functions[] = {
	{ "getvar", f_getvar },
	{ "setvar", f_setvar },
	{ "newvar", f_newvar },
	{ "con_print", f_print },
	{ "sin", f_sin },
	{ "cos", f_cos },
	{ "tan", f_tan },
	{ "log", f_log },
	{ "sqrt", f_sqrt },
	{ "action_activate", f_action_activate },
	{ "event_trigger", f_event_trigger },
	{ "log_warn", f_warn },
	{ "log_info", f_info },
	{ "log_dbug", f_dbug },
	{ "uptime", f_uptime },
	{ 0, 0 },
};


static void xlua_init_funcs(lua_State *L)
{
	const luaL_Reg *f = Functions;
	while (f->name) {
		lua_pushcfunction(L,f->func);
		lua_setglobal(L,f->name);
		++f;
	}
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
			Compiled.push_back(vn);
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
		const char *err = 0;
		if (0 == fstat(fd,&st)) {
			const char *buf = (const char *) malloc(st.st_size);
			int n = read(fd,buf,st.st_size);
			close(fd);
			if (st.st_size == n) {
				Lock lock(Mtx);
				if (0 == luaL_loadbuffer(LS,buf,st.st_size,vn)) {
					lua_setglobal(LS,vn);
					Compiled.push_back(vn);
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
		for (auto &x : Functions)
			t.println(x.name);
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
			Compiled.push_back(vn);
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
	fd = open(args[1],O_RDONLY);
	if (fd != -1) {
		struct stat st;
		const char *err = 0;
		if (0 == fstat(fd,&st)) {
			const char *buf = (const char *) malloc(st.st_size);
			int n = read(fd,buf,st.st_size);
			close(fd);
			if (st.st_size == n) {
				Lock lock(Mtx);
				if (0 == luaL_loadbuffer(LS,buf,st.st_size,vn)) {
					lua_setglobal(LS,vn);
					Compiled.push_back(vn);
					return 0;
				}
				t.println(lua_tostring(LS,-1));
				lua_pop(LS,1);
				return 1;
			} else {
				err = "read error";
			}
		} else {
			close(fd);
			err = "cannot stat file";
		}
		t.println(err);
		return 1;
	}
#endif
	return "Invalid argument #1.";
}


const char *xlua_exe(Terminal &t, const char *script)
{
	if (LS == 0)
		xlua_init();
	Lock lock(Mtx);
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
	lua_pop(LS,1);
	return r;
}


static void xlua_script(void *arg)
{
	if (arg == 0)
		return;
	Lock lock(Mtx);
	if (LS == 0)
		xlua_init();
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
	if (const char *str = lua_tostring(LS,-1))
		log_warn(TAG,"lua!run '%s': %s",script,str);
	lua_pop(LS,1);
	lua_gc(LS,LUA_GCCOLLECT);
}


static void xlua_init()
{
	LS = luaL_newstate();
	luaopen_base(LS);
	luaopen_math(LS);
	luaopen_string(LS);
	xlua_init_funcs(LS);
}


void xlua_setup()
{
	Mtx = xSemaphoreCreateMutex();
	action_add("lua!run",xlua_script,0,"run argument as Lua script");
	action_add("lua!file",xlua_script,0,"execute file with Lua interpreter");
	for (const auto &fn : Config.luafiles())
		xlua_parse_file(fn.c_str());
}

#endif
