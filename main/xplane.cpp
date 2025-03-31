/*
 *  Copyright (C) 2025, Thomas Maier-Komor
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

#ifdef CONFIG_XPLANE

#include "actions.h"
#include "env.h"
#include "globals.h"
#include "inetd.h"
#include "log.h"
#include "netsvc.h"
#include "shell.h"
#include "support.h"
#include "swcfg.h"
#include "terminal.h"
#include "timefuse.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <lwip/udp.h>

#include <esp_err.h>

#include <map>

#ifdef CONFIG_LUA
#include "luaext.h"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#endif


#define TAG MODULE_XPLANE

using namespace std;

struct XPlaneCtrl {
	int addDref(const char *alias, const char *dref, unsigned freq);

	EnvObject *env;
	ip_addr_t ip;
	uint16_t data_port, dref_port;
	udp_pcb *data_pcb;
	udp_pcb *dref_pcb;
	map<unsigned,EnvNumber *> varids;	// first is id<<8|idx
	map<estring,EnvNumber *> drefs;
	vector<EnvNumber *> dreqs;
};

struct iddata_t {
	uint32_t id;
	float value[8];
};


static XPlaneCtrl *Ctx = 0;
static event_t XpRdyEv = 0, XpUpdEv  = 0;
static bool Ready = false;


static void sendout(uint16_t port, uint8_t *data, size_t s)
{
	if ((0 == Ctx) || (0 == Ctx->dref_pcb))
		return;
	LWIP_LOCK();
	struct pbuf *r = pbuf_alloc(PBUF_TRANSPORT,s,PBUF_RAM);
	pbuf_take(r,data,s);
	int e = udp_sendto(Ctx->dref_pcb,r,&Ctx->ip,port);
	pbuf_free(r);
	LWIP_UNLOCK();
	if (e) {
		log_hex(TAG,data,s,"send packet");
		log_warn(TAG,"send %.*s: %s",s,data,strlwiperr(e));
	}
}


static void send_cmd(const char *cmd)
{
	size_t l = strlen(cmd) + 1;
	uint8_t packet[4+l];
	uint8_t *at = packet;
	memcpy(at,"CMND0",5);
	at += 5;
	memcpy(at,cmd,l);
	sendout(Ctx->dref_port,packet,sizeof(packet));
}


static void send_cmd_cb(void *arg)
{
	if (0 == arg) {
		log_warn(TAG,"send_cmd called without argument");
		return;
	}
	send_cmd((const char *)arg);
}


static void send_dref0(const char *dref, float val)
{
	size_t l = strlen(dref) + 1;
	uint8_t packet[509];
	memset(packet,' ',sizeof(packet));
	uint8_t *at = packet;
	memcpy(at,"DREF0",5);
	at += 5;
	memcpy(at,&val,sizeof(val));
	at += sizeof(val);
	memcpy(at,dref,l);
	sendout(Ctx->dref_port,packet,sizeof(packet));
}


static void send_dref0_cb(void *arg)
{
	if (0 == arg) {
		log_warn(TAG,"send_dref0 called without argument");
		return;
	}
	char dref[128];
	float val;
	if (2 != sscanf((const char *)arg,"%128s %f",dref,&val)) {
		log_warn(TAG,"invalid argument for send_dref0: %s",(const char *)arg);
		return;
	}
	log_dbug(TAG,"dref %s %f",dref,val);
	send_dref0(dref,val);
}



static void update_id(iddata_t *d)
{
	auto e = Ctx->varids.end();
	for (unsigned idx = 0; idx < sizeof(d->value)/sizeof(d->value[0]); ++idx) {
		auto i = Ctx->varids.find(d->id<<8|idx);
		if (i != e) {
			EnvNumber *n = i->second;
			if (d->value[idx] != n->get()) {
				n->set(d->value[idx]);
				event_trigger(XpUpdEv);
			}
		}
	}
}


static void data_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *buf, const ip_addr_t *ip, u16_t port)
{
	// in lwip locked context!
	if (0 == memcmp(buf->payload,"DATA",4)) {
//		log_dbug(TAG,"received data packet");
		const uint8_t *data = (const uint8_t *)buf->payload+5;
		size_t size = buf->len-5;
		while (size >= 36) {
			iddata_t *f = (iddata_t *)data;
			log_dbug(TAG,"id %3u: %f, %f, %f, %f, %f",f->id,f->value[0],f->value[1],f->value[2],f->value[3],f->value[4]);
			update_id(f);
			data += 36;
			size -= 36;
		}
		if (!Ready) {
			Ready = true;
			event_trigger(XpRdyEv);
		}
	} else {
		log_warn(TAG,"unhandled packet type %.4s",(const char *)buf);
	}
	pbuf_free(buf);
}


static void rref_update(struct pbuf *buf)
{
	unsigned at = 5;
	log_hex(TAG,buf->payload,buf->len,"rref update, len %d",buf->len);
	struct RREF {
		uint32_t id;
		float val;
	};
	//RREF *r = (RREF *) buf->payload+5;
	while (at + sizeof(RREF) <= buf->len) {
		RREF ref, *r = &ref;
		memcpy(r,(char*)buf->payload+at,sizeof(ref));
		if (Ctx->dreqs.size() > r->id) {
			EnvNumber *e = Ctx->dreqs[r->id];
			log_dbug(TAG,"rref update for %s: %f",e->name(),(double)r->val);
			if (e->get() != r->val) {
				e->set(r->val);
				event_trigger(XpUpdEv);
			}
			if (!Ready) {
				Ready = true;
				event_trigger(XpRdyEv);
			}
		} else {
			log_warn(TAG,"rref id 0x%x is out of range",r->id);
		}
		at += sizeof(ref);
	}
}


static void dref_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *buf, const ip_addr_t *ip, u16_t port)
{
	if (0 == memcmp(buf->payload,"DREF+",5)) {
		//log_hex(TAG,buf->payload,buf->len,"dref+ update, len %d",buf->len);
		const char *dname = (const char *) buf->payload+9;
		float f;
		memcpy(&f,(char*)buf->payload+5,sizeof(float));
		auto i = Ctx->drefs.find(dname);
		if (i != Ctx->drefs.end()) {
			if (i->second->get() != f) {
				log_dbug(TAG,"dref+ %f '%s' updated",f,dname);
				i->second->set(f);
				event_trigger(XpUpdEv);
			}
			if (!Ready) {
				Ready = true;
				event_trigger(XpRdyEv);
			}
		} else {
			log_dbug(TAG,"dref+ %f '%s' not found",f,dname);
		}
	} else if (0 == memcmp(buf->payload,"RREF",4)) {
		rref_update(buf);
	} else {
		log_hex(TAG,buf->payload,buf->len,"unhandled packet type %.4s",(const char *)buf);
	}
	pbuf_free(buf);
}


static void req_dref(unsigned id, unsigned freq, const char *dref)
{
	/* x-plane spec
	HEADER : RREF\0
	struct dref_in {
		uint32_t freq, id;
		char name[400];
	}
	=> size 413
	*/
	log_dbug(TAG,"req dref %s",dref);
	char buf[413];
	memset(buf,sizeof(buf),' ');
	char *at = buf;
	memcpy(at,"RREF",5);	// including \0!
	at += 5;
	memcpy(at,&freq,sizeof(uint32_t));
	at += sizeof(uint32_t);
	memcpy(at,&id,sizeof(uint32_t));
	at += sizeof(uint32_t);
	strncpy(at,dref,400);
	sendout(Ctx->dref_port,(uint8_t*)buf,sizeof(buf));
}


int XPlaneCtrl::addDref(const char *alias, const char *dref, unsigned freq)
{
	if (0 == is_id(alias)) {
		log_warn(TAG,"invalid alias '%s'",alias);
		return 1;
	}
	EnvElement *e = env->getChild(alias);
	EnvNumber *n;
	if (0 == e) {
		n = env->add(alias,0.0);
	} else {
		n = e->toNumber();
		assert(n);
	}
	drefs.insert(pair<estring,EnvNumber*>(dref,n));
	dreqs.push_back(n);
	return 0;
}


void connect(void *)
{
	log_info(TAG,"connect");
	const XPlaneConfig &c = Config.xplane();
	unsigned id = 0;
	for (auto dref : c.drefs()) {
		const estring &name = dref.name();
		req_dref(id,dref.freq(),name.c_str());
		++id;
	}
}


#ifdef CONFIG_LUA
static int luax_xplane_command(lua_State *L)
{
	const char *c = luaL_checkstring(L,1);
	log_dbug(TAG,"send command %s",c);
	send_cmd(c);
	return 0;
}


static int luax_xplane_send_dref(lua_State *L)
{
	const char *n = luaL_checkstring(L,1);
	float v = luaL_checknumber(L,2);
	log_dbug(TAG,"send dref %s %f",n,v);
	send_dref0(n,v);
	return 0;
}


static const LuaFn Functions[] = {
	{ "xplane_dref", luax_xplane_send_dref, "set dref value" },
	{ "xplane_command", luax_xplane_command, "execute xplane command" },
	{ 0, 0, 0 }
};
#endif


const char *xplane(Terminal &term, int argc, const char *args[])
{
	if (0 == Ctx) {
		return "Not initilized.";
	}
	if (argc == 1) {
		return "Invalid number of arguments.";
	} else if (argc == 2) {
		if (0 == strcmp(args[1],"drefs")) {
			for (auto d : Ctx->drefs) {
				term.println(d.first.c_str());
			}
		} else if (0 == strcmp(args[1],"dref")) {
			return "Missing argument.";
		} else {
			return "Invalid arguments #1.";
		}
	} else if (argc == 5) {
		if (0 == strcmp(args[1],"dref")) {
			char *e;
			long l = strtol(args[4],&e,0);
			if ((*e) || (l <= 0))
				return "Invalid arguments #4.";
			if (Ctx->addDref(args[2],args[3],l))
				return "Invalid arguments #2.";
		}
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}


void xplane_setup()
{
	if (!Config.has_xplane())
		return;
	const XPlaneConfig &c = Config.xplane();
	if (!c.has_dataport() && !c.has_refport() && c.dataids().empty() && !c.drefs().empty())
		return;
	log_info(TAG,"setup");
	Ctx = new XPlaneCtrl;
	EnvObject *env = new EnvObject("xplane");
	RTData->add(env);
	Ctx->env = env;
	XpRdyEv = event_register("xplane`ready");
	XpUpdEv = event_register("xplane`update");
	uint32_t ip4 = c.ip4addr();
	Ctx->ip = IPADDR4_INIT(ip4);
	if (c.has_dataport()) {
		LWIP_LOCK();
		Ctx->data_pcb = udp_new();
		Ctx->data_port = c.dataport();
		udp_recv(Ctx->data_pcb,data_recv_cb,0);
		udp_bind(Ctx->data_pcb,IP_ANY_TYPE,Ctx->data_port);
		LWIP_UNLOCK();
	}

	if (c.has_refport()) {
		LWIP_LOCK();
		Ctx->dref_pcb = udp_new();
		Ctx->dref_port = c.refport();
		udp_bind(Ctx->dref_pcb,IP_ANY_TYPE,Ctx->dref_port);
		udp_recv(Ctx->dref_pcb,dref_recv_cb,0);
		LWIP_UNLOCK();
		action_add("xplane!dref",send_dref0_cb,0,"X-plane: send dref");
		action_add("xplane!cmd",send_cmd_cb,0,"X-plane: send command");
	}
	
	if (!c.dataids().empty()) {
		for (auto did : c.dataids()) {
			const estring &name = did.varname();
			unsigned id = did.id();
			unsigned idx = did.idx();
			EnvNumber *n = env->add(name.c_str(),0.0);
			Ctx->varids.insert(make_pair(id<<8|idx,n));
		}
	}

	if (!c.drefs().empty()) {
		for (auto dref : c.drefs()) {
			Ctx->addDref(dref.alias().c_str(),dref.name().c_str(),dref.freq());
		}
	}
	Action *a = action_add("xplane!connect",connect,0,"request data from server");
	event_callback(event_id("wifi`got_ip"),a);
	timefuse_t t = timefuse_create("xplanetmr",1000,true);
	event_t e = timefuse_timeout_event(t);
	event_callback(e,a);
	event_callback("wifi`got_ip","xplanetmr!start");
#ifdef CONFIG_LUA
	xlua_add_funcs("xplane",Functions);
#endif
}


#endif
