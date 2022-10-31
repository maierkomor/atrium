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

#ifdef CONFIG_STATEMACHINES

#include "actions.h"
#include "env.h"
#include "event.h"
#include "globals.h"
#include "swcfg.h"
#include "terminal.h"
#include "log.h"

#include <vector>


#define TAG MODULE_SM

using namespace std;


struct State
{
	void init(const StateConfig &c, const char *machname);
	estring name;
	event_t enter,exit;
	vector<trigger_t> conds;
};


class StateMachine
{
	public:
	explicit StateMachine(const StateMachineConfig &);
	explicit StateMachine(const char *);
	
	void switch_state(const char *);

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

	void addState(const StateConfig &st);
	void addState(const char *st);

	const estring m_name;
	vector<State> m_states;
	private:
	StateMachine *m_next = 0;
	int8_t m_st = -1;
	static StateMachine *First;
	friend void sm_set_state(void*);
};


StateMachine *StateMachine::First = 0;


void State::init(const StateConfig &cfg, const char *stname)
{
//	log_dbug(TAG,"init state %s",stname);
//	name = stname;
	name = cfg.name().c_str();
	enter = event_register(stname,"`enter");
	exit = event_register(stname,"`exit");
	for (const auto &x : cfg.conds()) {
		event_t e = event_id(x.event().c_str());
		if (e == 0)
			continue;
		for (const auto &ac : x.action()) {
			char an[ac.size()+1];
			strcpy(an,ac.c_str());
			char *sp = strchr(an,' ');
			if (sp)
				*sp = 0;
			if (Action *a = action_get(an)) {
				log_dbug(TAG,"adding %s => %s",event_name(e),a->name);
				trigger_t t;
				if (sp) {
					t = event_callback_arg(e,a,sp+1);
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


void sm_set_state(void *arg)
{
	if (0 == arg) 
		return;
	char *sm = (char *) arg;
	char *c = strchr(sm,':');
	if (c == 0)
		return;
	*c = 0;
	char *st = c+1;
	log_dbug(TAG,"switch state of %s to %s",sm,st);
	StateMachine *m = StateMachine::first();
	while (m) {
		if (0 == strcmp(sm,m->m_name.c_str())) {
			m->switch_state(st);
			break;
		}
		m = m->m_next;
	}
	if (m == 0)
		log_warn(TAG,"switch state of %s to %s: unknown state-machine",sm,st);
	free(arg);
}


StateMachine::StateMachine(const char *n)
: m_name(n)
, m_next(First)
{
	if (0 == First)
		action_add("sm!set",sm_set_state,0,"set state of state-machine to <machine>:<state>");
	First = this;
}


StateMachine::StateMachine(const StateMachineConfig &cfg)
: m_name(cfg.name())
, m_next(First)
{
	if (0 == First) {
		log_dbug(TAG,"add sm!set");
		action_add("sm!set",sm_set_state,0,"set state of state-machine to <machine>:<state>");
	}
	for (const auto &st : cfg.states())
		addState(st);
	First = this;
	int numst = cfg.states_size();
	if (numst > 0) {
		uint8_t x = cfg.ini_st();
		if (x >= numst)
			x = 0;
		switch_state(cfg.states(x).name().c_str());
	}
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
	m_states.emplace_back();
	m_states.back().init(st,stn);
	log_dbug(TAG,"added %s",stn);
}


void StateMachine::addState(const char *n)
{
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


void StateMachine::switch_state(const char *nst)
{
	uint8_t x = 0;
	for (const auto &s : m_states) {
		if (0 == strcmp(s.name.c_str(),nst))
			break;
		++x;
	}
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
		m_st = x;
	} else {
		log_warn(TAG,"switch state of %s to %s: unknown state",m_name.c_str(),nst);
	}

}


const char *sm_cmd(Terminal &term, int argc, const char *args[])
{
	if (argc == 1) {
		StateMachine *m = StateMachine::first();
		if (m == 0)
			term.println("no state-machines defined");
		while (m) {
			term.printf("%s: %s\n",m->m_name.c_str(),m->active_state());
			for (const auto &st : m->m_states) {
				term.printf("\t%s\n",st.name.c_str());
			}
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
		if (argc == 5)
			t = event_callback(e,a);
		else
			t = event_callback_arg(e,a,args[5]);
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


void sm_setup()
{
	for (const auto &sm : Config.statemachs()) {
		if (sm.name().empty() || (sm.states().empty()))
			continue;
		log_info(TAG,"add statemachine %s",sm.name().c_str());
		new StateMachine(sm);
	}
}

#endif
