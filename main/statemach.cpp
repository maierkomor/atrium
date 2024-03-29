/*
 *  Copyright (C) 2022-2024, Thomas Maier-Komor
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

#ifdef CONFIG_STATEMACHINES

#include "actions.h"
#include "env.h"
#include "event.h"
#include "globals.h"
#include "nvm.h"
#include "swcfg.h"
#include "terminal.h"
#include "log.h"

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif

#include <vector>


#define TAG MODULE_SM

using namespace std;


struct State
{
	State()
	{ }

	State(const StateConfig &c, const char *machname)
	{ init(c,machname); }

	void init(const StateConfig &c, const char *machname);
	estring name;
	uint8_t id;
	event_t enter,exit;
	vector<trigger_t> conds;
};


class StateMachine
{
	public:
	explicit StateMachine(const StateMachineConfig &);
	explicit StateMachine(const char *);
	
	int switch_state(const char *);

	static StateMachine *first()
	{ return First; }

	StateMachine *next() const
	{ return m_next; }

	const char *active_state()
	{
		if (m_st == -1)
			return "<none>";
		return m_states[m_st].name.c_str();
	}

	static StateMachine *get(const char *n)
	{
		StateMachine *r = First;
		while (r) {
			if (r->m_name == n)
				break;
			r = r->m_next;
		}
		return r;
	}


	State *getState(const char *n)
	{
		for (auto &st : m_states) {
//			log_dbug(TAG,"%s == %s",st.name.c_str(),n);
			if (st.name == n)
				return &st;
		}
		return 0;
	}

	const vector<State> &getStates() const
	{ return m_states; }

	void attach(EnvObject *r);

	void addState(const StateConfig &st);
	void addState(const char *st);
	static void set_state(void*);
	static void set_next(void*);

	const estring m_name;

	private:
	EnvString m_env;
	vector<State> m_states;
	StateMachine *m_next = 0;
	int8_t m_st = -1;
	bool m_persistent = false;
	static StateMachine *First;
};


StateMachine *StateMachine::First = 0;


void State::init(const StateConfig &cfg, const char *stname)
{
	log_dbug(TAG,"init state %s",stname);
//	name = stname;
	name = cfg.name().c_str();
	enter = event_register(stname,"`enter");
	exit = event_register(stname,"`exit");
	for (const auto &x : cfg.conds()) {
		event_t e = event_id(x.event().c_str());
		if (e == 0) {
			log_warn(TAG,"unknown event %s requested for state %s",x.event().c_str(),stname);
			continue;
		}
		for (const auto &ac : x.action()) {
			char an[ac.size()+1];
			strcpy(an,ac.c_str());
			char *sp = strchr(an,' ');
			char *arg = 0;
			if (sp) {
				*sp = 0;
				++sp;
				arg = strdup(sp);
			}
			if (Action *a = action_get(an)) {
				log_dbug(TAG,"adding %s => %s",event_name(e),a->name);
				trigger_t t;
				if (sp) {
					t = event_callback_arg(e,a,arg);
				} else {
					t = event_callback(e,a);
				}
				event_trigger_en(t,false);
				conds.push_back(t);
			} else {
				log_warn(TAG,"unable to find action %s",ac.c_str());
			}
		}
	}

}


void StateMachine::set_state(void *arg)
{
	if (0 == arg) 
		return;
	char *sm = (char *) arg;
	size_t l = strlen(sm);
	char ns[l+1];
	strcpy(ns,sm);
	char *c = strchr(ns,':');
	if (c == 0)
		return;
	*c = 0;
	char *st = c+1;
	log_dbug(TAG,"switch state of %s to %s",ns,st);
	StateMachine *m = StateMachine::first();
	while (m) {
		if (0 == strcmp(ns,m->m_name.c_str())) {
			m->switch_state(st);
			return;
		}
		m = m->m_next;
	}
	log_warn(TAG,"switch state of %s to %s: unknown state-machine",sm,st);
}


void StateMachine::set_next(void *arg)
{
	if (0 == arg) 
		return;
	char *sm = (char *) arg;
	log_dbug(TAG,"next state of %s",sm);
	StateMachine *m = StateMachine::first();
	while (m) {
		if (0 == strcmp(sm,m->m_name.c_str())) {
			if (!m->m_states.empty()) {
				unsigned x = m->m_st;
				++x;
				if (x == m->m_states.size())
					x = 0;
				m->switch_state(m->m_states[x].name.c_str());
			}
			return;
		}
		m = m->m_next;
	}
	log_warn(TAG,"next state of %s: unknown state-machine",sm);
}


StateMachine::StateMachine(const char *n)
: m_name(n)
, m_env(n,"")
, m_next(First)
{
	if (0 == First) {
		action_add("sm!set",set_state,0,"set state of state-machine to <machine>:<state>");
		action_add("sm!next",set_next,0,"set next state of state-machine <machine>");
	}
	First = this;
}


StateMachine::StateMachine(const StateMachineConfig &cfg)
: m_name(cfg.name())
, m_env(cfg.name().c_str(),"")
, m_next(First)
, m_persistent(cfg.persistent())
{
	if (0 == First) {
		action_add("sm!set",set_state,0,"set state of state-machine to <machine>:<state>");
		action_add("sm!next",set_next,0,"set next state of state-machine <machine>");
	}
	for (const auto &st : cfg.states())
		addState(st);
	First = this;
}


void StateMachine::addState(const StateConfig &st)
{
	size_t mnl = m_name.size();
	const char *n = m_name.c_str();
	size_t stnl = st.name().size()+1;
	char stn[mnl+stnl+1];
	memcpy(stn,n,mnl);
	stn[mnl] = ':';
	memcpy(stn+mnl+1,st.name().c_str(),stnl);
	m_states.emplace_back(st,(char*)stn);
	log_dbug(TAG,"added %s",stn);
}


void StateMachine::addState(const char *n)
{
	log_dbug(TAG,"sm %s add state %s",m_name.c_str(),n);
	size_t mnl = m_name.size();
	const char *mn = m_name.c_str();
	size_t stnl = strlen(n)+1;
	char stn[mnl+stnl+1];
	memcpy(stn,mn,mnl);
	stn[mnl] = ':';
	memcpy(stn+mnl+1,n,stnl);
	m_states.emplace_back();
	State &st = m_states.back();
	st.name = n;
	st.enter = event_register(stn,"`enter");
	st.exit = event_register(stn,"`exit");
	for (StateMachineConfig &cfg : *Config.mutable_statemachs()) {
		if (cfg.name() == m_name) {
			StateConfig *sc = cfg.add_states();
			sc->set_name(n);
			return;
		}
	}
}


void StateMachine::attach(EnvObject *r)
{
	r->add(&m_env);
}


int StateMachine::switch_state(const char *nst)
{
	uint8_t x = 0;
	for (const auto &s : m_states) {
		if (0 == strcmp(s.name.c_str(),nst))
			break;
		++x;
	}
#if 0
	// we don't want to hide state self-transisitons,
	// as this would hide the exit and enter events
	if (x == m_st)
		return 0;
#endif
	if (x < m_states.size()) {
		// de-register old 
		if (m_st >= 0) {
			log_dbug(TAG,"%s: leaving state %s",m_name.c_str(),m_states[m_st].name.c_str());
			for (auto t : m_states[m_st].conds)
				event_trigger_en(t,false);
			event_trigger(m_states[m_st].exit);
		}
		// register new
		event_trigger(m_states[x].enter);
		for (auto t : m_states[x].conds)
			event_trigger_en(t,true);
		log_dbug(TAG,"%s: new state %s",m_name.c_str(),nst);
		m_env.set(nst);
		if (m_persistent)
			nvm_store_u8(m_name.c_str(),x);
		m_st = x;
		return 0;
	} else {
		log_warn(TAG,"switch state of %s to %s: unknown state",m_name.c_str(),nst);
		return 1;
	}

}


static void sm_print_sm(Terminal &term, StateMachine *m)
{
	term.printf("%s: %s\n",m->m_name.c_str(),m->active_state());
	for (const auto &st : m->getStates()) {
		term.printf("\t%s:\n",st.name.c_str());
		for (auto t : st.conds) {
			const char *arg = trigger_get_arg(t);
			if (arg == 0)
				arg = "";
			term.printf("\t\t%s => %s(%s)\n",trigger_get_eventname(t),trigger_get_actionname(t),arg);
		}
	}
}


const char *sm_cmd(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		StateMachine *m = StateMachine::first();
		if (m == 0)
			term.println("no state-machines defined");
		while (m) {
			sm_print_sm(term,m);
			m = m->next();
		}
		return 0;
	}
	if (0 == strcmp(args[1],"add")) {
		if (0 == term.getPrivLevel())
			return "Access denied.";
		if (argc == 2)
			return "Invalid number of arguments.";
		StateMachine *m = StateMachine::get(args[2]);
		if (argc == 3) {
			if (m || strchr(args[2],':'))
				return "Invalid argument #2.";
			new StateMachine(args[2]);
			Config.add_statemachs()->set_name(args[2]);
		} else if (argc == 4) {
			if ((m == 0) || (m->getState(args[3])))
				return "Invalid argument #2.";
			m->addState(args[3]);
		} else {
			return "Invalid argument #2.";
		}
	} else if (0 == strcmp(args[1],"on")) {
		if (0 == term.getPrivLevel())
			return "Access denied.";
		if ((argc < 5) || (argc > 6))
			return "Invalid number of arguments.";
		char *c = strchr(args[2],':');
		if (c == 0)
			return "Invalid argument #2.";
		*c = 0;
		StateMachine *m = StateMachine::get(args[2]);
		if (m == 0)
			return "Invalid argument #2.";
		State *s = m->getState(c+1);
		if (s == 0)
			return "Unknown state.";
		event_t e = event_id(args[3]);
		if (e == 0)
			return "Invalid argument #3.";
		Action *a = action_get(args[4]);
		if (a == 0)
			return "Invalid argument #4.";
		char arg[strlen(args[4]) + argc == 5 ? strlen(args[5]) : 0 + 2];
		trigger_t t;
		t = event_callback_arg(e,a, (argc==5) ? 0 : args[5]);
		event_trigger_en(t,false);
		s->conds.push_back(t);
		strcpy(arg,args[4]);
		if (argc == 6) {
			strcat(arg," ");
			strcat(arg,args[5]);
		}
		for (auto &smc : *Config.mutable_statemachs()) {
			if (smc.name() != m->m_name)
				continue;
			for (StateConfig &sc : *smc.mutable_states()) {
				if (sc.name() != s->name)
					continue;
				for (Trigger &t : *sc.mutable_conds()) {
					if (t.event() == args[3]) {
						t.add_action(arg);
						return 0;
					}
				}
				Trigger *t = sc.add_conds();
				t->set_event(args[3]);
				t->add_action(arg);
				return 0;
			}
		}
		return "Config update failed.";
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


#ifdef CONFIG_LUA
static int luax_sm_set(lua_State *L)
{
	StateMachine *sm = StateMachine::get(luaL_checkstring(L,1));
	if (sm == 0) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	const char *st = luaL_checkstring(L,2);
	if (sm->switch_state(st)) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	return 0;
}


static int luax_sm_get(lua_State *L)
{
	StateMachine *sm = StateMachine::get(luaL_checkstring(L,1));
	if (sm == 0) {
		lua_pushliteral(L,"Invalid argument #1.");
		lua_error(L);
	}
	lua_pushstring(L,sm->active_state());
	return 1;
}


static const LuaFn Functions[] = {
	{ "sm_set", luax_sm_set, "set state-machine state (sm,state)" },
	{ "sm_get", luax_sm_get, "get state-machine state (sm)" },
	{ 0, 0, 0 }
};
#endif	// CONFIG_LUA


void sm_setup()
{
	if (Config.statemachs().empty())
		return;
	EnvObject *root = RTData->add("sm");
	for (const auto &sm : Config.statemachs()) {
		if (sm.name().empty() || (sm.states().empty()))
			continue;
		log_info(TAG,"add statemachine %s",sm.name().c_str());
		StateMachine *m = new StateMachine(sm);
		m->attach(root);
	}
#ifdef CONFIG_LUA
	xlua_add_funcs("sm",Functions);
#endif
}


void sm_start()
{
	for (const auto &cfg : Config.statemachs()) {
		if (cfg.name().empty() || cfg.states().empty())
			continue;
		int numst = cfg.states_size();
		uint8_t x = cfg.ini_st();
		if (x >= numst)
			x = 0;
		const char *name = cfg.name().c_str();
		if (cfg.persistent())
			x = nvm_read_u8(name,x);
		if (x < cfg.states_size()) {
			if (StateMachine *m = StateMachine::get(name))
				m->switch_state(cfg.states(x).name().c_str());
		}
	}
}
#endif
