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

#ifdef CONFIG_SYSLOG

#include "actions.h"
#include "swcfg.h"
#include "event.h"
#include "globals.h"
#include "log.h"
#include "netsvc.h"
#include "shell.h"
#include "syslog.h"
#include "terminal.h"
#include "wifi.h"

#include <string.h>

#include <lwip/tcpip.h>
#include <lwip/inet.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define SYSLOG_PORT 514
#define PBUF_SIZE 256
#define MAX_FRAME_SIZE 1472

#define STRINGLIT_CONCAT(a,b) a b

#if 0
#define log_devel(tag,msg,...) con_printf(STRINGLIT_CONCAT(msg,"\n"),__VA_ARGS__)
//#define log_devel(tag,msg,...) log_dbug(tag,msg,__VA_ARGS__)
#else
#define log_devel(...)
#endif


struct LogMsg
{
	LogMsg *next;
	uint32_t ts;
	logmod_t mod;
	uint8_t ml;
	log_level_t lvl;
	bool ntp;		// true: sec since 1970, false msec since start
	char sent;		// 'L': local, do not send; '*': unset; ' ': sent
	char msg[];		// not 0-terminated
};

struct Syslog
{
	explicit Syslog(size_t s);
	~Syslog();

	struct udp_pcb *pcb = 0;
	LogMsg *first = 0, *last = 0;
	uint16_t maxsize, alloc = 0;
	uint16_t unsent = 0, overwr = 0;
	uint32_t sent = 0;
	event_t ev;
	bool triggered = false;

	private:
	Syslog(const Syslog &);
	Syslog& operator = (const Syslog &);
};

#define TAG MODULE_LOG
static SemaphoreHandle_t Mtx = 0;
static Syslog *Ctx = 0;


static void syslog_start(void*);


Syslog::Syslog(size_t s)
: ev(event_id("syslog`msg"))
{
	if ((s > 0) && (s < 512))
		s = 512;
	else if (s > UINT16_MAX)
		s = UINT16_MAX;
	maxsize = s;
}


Syslog::~Syslog()
{
	if (pcb) {
		LWIP_LOCK();
		udp_remove(pcb);
		LWIP_UNLOCK();
	}
	LogMsg *m = first;
	while (m) {
		LogMsg * n = m->next;
		delete m;
		m = n;
	}
}


static int sendmsg(LogMsg *m)
{
	int n;
	char header[64];
	const char *mod = ModNames+ModNameOff[m->mod];
	if (m->ntp) {
		struct tm tm;
		time_t ts = m->ts;
		gmtime_r(&ts,&tm);
		n = snprintf(header,sizeof(header),"<%d>1 %4u-%02u-%02uT%02u:%02u:%02u.%03u %.*s %s - - - "
			, 16 << 3 | (m->lvl+3)	// facility local use = 16, pri = (facility)<<3|serverity
			, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday
			, tm.tm_hour, tm.tm_min, tm.tm_sec, m->ts%1000
			, HostnameLen
			, Hostname
			, mod
			);
	} else {
		n = snprintf(header,sizeof(header),"<%d>1 - %.*s %s - - - "
			, 16 << 3 | (m->lvl+3)	// facility local use = 16, pri = (facility)<<3|serverity
			, HostnameLen
			, Hostname
			, mod
			);
	}
	assert(n < sizeof(header));
	log_devel(TAG,"send %.*s",m->ml,m->msg);
	LWIP_LOCK();	//-- this caused a hang - why? still true?
	struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, n+m->ml, PBUF_RAM);
	pbuf_take(pbuf,header,n);
	pbuf_take_at(pbuf,m->msg,m->ml,n);
	err_t e = udp_send(Ctx->pcb,pbuf);
	pbuf_free(pbuf);
	LWIP_UNLOCK();
	if (e) {
		log_dbug(TAG,"sendto %d",e);
	}
	return e;
}


static void sendall(void * = 0)
{
	if (Ctx == 0)
		return;
	if (Ctx->pcb == 0) {
		log_dbug(TAG,"no pcb");
		syslog_start(0);	// hangs on startup on IDF v4.x with station connect not finishing
		return;
	}
	log_dbug(TAG,"sendall");
	unsigned x = 0;
	MLock lock(Mtx);
	LogMsg *m = Ctx->first;
	while (m) {
		if (m->sent == '*') {
			m->sent = 's';
			lock.unlock();
			int r = sendmsg(m);
			if (r) {
				m->sent = '*';
				goto done;
//				if (x) {
//					vTaskDelay(10);	// needed for UDP stack to catch up
//					lock.lock();
//					continue;
//				} else
//					break;
			}
			lock.lock();
			m->sent = ' ';
			--Ctx->unsent;
			++Ctx->sent;
			++x;
		}
		m = m->next;
	}
	Ctx->triggered = false;
	lock.unlock();
done:
	log_dbug(TAG,"sent %u log messages",x);
}


static void syslog_hostip(const char *hn, const ip_addr_t *ip, void *arg)
{
	// no LWIP_LOCK as called from tcpip_task
	log_dbug(TAG,"connect %s at %s",hn,inet_ntoa(ip));
	err_t e;
	udp_pcb *pcb;
	{
		Lock lock(Mtx,__FUNCTION__);
		if (Ctx->pcb) {
			pcb = Ctx->pcb;
			Ctx->pcb = 0;
			udp_remove(pcb);
		}
		pcb = udp_new();
		e = udp_connect(pcb,ip,SYSLOG_PORT);
		if (e) {
			udp_remove(pcb);
			pcb = 0;
		}
	}
	if (e) {
		log_warn(TAG,"udp_connect %d",e);
	} else {
		Ctx->pcb = pcb;
		if (Ctx->ev == 0)
			Ctx->ev = event_id("syslog`msg");
		if (Ctx->ev != 0)
			event_trigger_nd(Ctx->ev);
	}
}


static void syslog_start(void*)
{
	if ((Ctx == 0) || (StationMode != station_connected))
		return;
	if (!Config.has_syslog_host())
		return;
	const char *host = Config.syslog_host().c_str();
	err_t e = query_host(host,0,syslog_hostip,0);
	if (e < 0)
		log_warn(TAG,"query host %s: %d",host,e);
	else
		log_dbug(TAG,"start");
}


static LogMsg *create_msg(uint8_t l)
{
	// assume lock is alread taken
	log_devel(TAG,"%u+%u < %u",Ctx->alloc,l,Ctx->maxsize);
	bool rfail = false;
	while (((Ctx->alloc + sizeof(LogMsg) + l > Ctx->maxsize) && Ctx->first) || rfail) {
		LogMsg *r = Ctx->first, *p = 0;
		unsigned skip = 0;
		while (r->sent == 's') {
			// skip message that is currently being sent
			log_devel(TAG,"skip %.*s",r->ml,r->msg);
			++skip;
			p = r;
			r = r->next;
			if (r == 0)
				return 0;
		}
		log_devel(TAG,"remove %c %d %p->%p, skip %u\n",r->sent,r->ml,p,r->next,skip);
		assert((p == 0) || (skip != 0));
		rfail = false;
		if (r->next == 0)
			Ctx->last = p;
		if (Ctx->first == r)
			Ctx->first = r->next;
		if (p)
			p->next = r->next;
		Ctx->alloc -= r->ml;
		if (r->sent == '*') {
			--Ctx->unsent;
			++Ctx->overwr;
		}
		// realloc seems to be buggy...!
		if (Ctx->alloc + l <= Ctx->maxsize) {
			log_devel(TAG,"recycle %u for %u",r->ml,l);
			if (r->ml == l) {
				Ctx->alloc += l;
				return r;
			}
			if (void *x = realloc(r,sizeof(LogMsg)+l)) {
				Ctx->alloc += l;
				return (LogMsg *) x;
			}
			log_devel(TAG,"realloc failed");
			rfail = true;
		}
		log_devel(TAG,"free %u",r->ml);
		free(r);
		Ctx->alloc -= sizeof(LogMsg);
	}
	LogMsg *r = (LogMsg *) malloc(sizeof(LogMsg)+l);
	if (r)
		Ctx->alloc += sizeof(LogMsg)+l;
	return r;
}


extern "C"
void log_syslog(log_level_t lvl, logmod_t module, const char *msg, size_t ml, struct timeval *tv)
{
	// header: pri version timestamp hostname app-name procid msgid
	if (Ctx == 0)
		return;
	if (ml > INT8_MAX)	// could be UINT8_MAX, reserved for now
		ml = INT8_MAX;
	if (pdTRUE != xSemaphoreTake(Mtx,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(Mtx,__FILE__);
	bool trigger = false;
	if (LogMsg *m = create_msg(ml)) {
		if (Ctx->last)
			Ctx->last->next = m;
		else
			Ctx->first = m;
		Ctx->last = m;
		m->next = 0;
		m->lvl = lvl;
		m->ml = ml;
		m->mod = module;
		memcpy(m->msg,msg,ml);
		if (ml == INT8_MAX)
			memcpy(m->msg+INT8_MAX-5,"[...]",5);
		struct timeval tv2;
		if (tv == 0) {
			gettimeofday(&tv2,0);
			tv = &tv2;
		}
		m->ntp = tv->tv_sec > 10000000;
		if (m->ntp)
			m->ts = tv->tv_sec;
		else
			m->ts = tv->tv_sec * 1000 + tv->tv_usec/1000;
		if (lvl == ll_local) {
			m->sent = 'L';
		} else {
			m->sent = '*';
			++Ctx->unsent;
			trigger = !Ctx->triggered && (Ctx->ev != 0);
			Ctx->triggered = true;
		}
	} else {
		con_print("syslog: OUT OF MEMORY (OOM)");
	}
	xSemaphoreGive(Mtx);
	if (trigger)
		event_trigger_nd(Ctx->ev);
}


int dmesg(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return arg_invnum(term);;
	if (argc == 2) {
		char *eptr;
		long l = strtol(args[1],&eptr,0);
		if ((eptr == args[1]) || (l < 0)) {
			return arg_invalid(term,args[1]);
		}
		if (((l > 0) && (l < 512)) || (l > UINT16_MAX))
			return arg_invalid(term,args[1]);
		Config.set_dmesg_size(l);
		dmesg_resize(l);
		return 0;
	}
	if (Ctx == 0) {
		term.printf("dmesg is inactive\n");
		return 1;
	}
	Lock lock(Mtx,__FUNCTION__);		// would need a recursive lock to support execution with debug on lwtcp
	LogMsg *m = Ctx->first;
	term.printf("%u/%u bytes, %u queued, %u overwritten, %u sent\n",Ctx->alloc,Ctx->maxsize,Ctx->unsent,Ctx->overwr,Ctx->sent);
	while (m) {
		const char *mod = ModNames+ModNameOff[m->mod];
		if (m->ntp) {
			struct tm tm;
			time_t ts = m->ts;
			gmtime_r(&ts,&tm);
			term.printf("%c %02u:%02u:%02u.%03lu %-8s: %.*s\n"
				, m->sent
				, tm.tm_hour, tm.tm_min, tm.tm_sec, m->ts%1000
				, mod
				, m->ml
				, m->msg
				);
		} else {
			term.printf("%c %8u.%03lu %-8s: %.*s\n"
				, m->sent
				, m->ts/1000, m->ts%1000
				, mod
				, m->ml
				, m->msg
				);
		}
		m = m->next;
	}
	return 0;
}


void dmesg_setup()
{
	Mtx = xSemaphoreCreateMutex();
	event_register("syslog`msg");
	dmesg_resize(2048);
}


void dmesg_resize(size_t ds)
{
	log_dbug(TAG,"resize %u",ds);
	Lock lock(Mtx,__FUNCTION__);
	if (Ctx == 0) {
		if (ds)
			Ctx = new Syslog(ds);
	} else {
		while (Ctx->alloc > ds) {
			LogMsg *r = Ctx->first;
			assert(r);
			Ctx->first = r->next;
			Ctx->alloc -= r->ml + sizeof(LogMsg);
			free(r);
		}
		if (ds == 0) {
			delete Ctx;
			Ctx = 0;
		} else {
			Ctx->maxsize = ds;
		}
	}
}


void syslog_setup(void)
{
	log_dbug(TAG,"setup");
	action_add("syslog!start",syslog_start,0,0);	// do not advertise - not needed to call manually
	event_callback("wifi`station_up","syslog!start");
	Action *a = action_add("syslog!send",sendall,0,"trigger sendind dmesg to syslog");
	event_t e = event_id("syslog`msg");
	assert(e);
	event_callback(e,a);
	if (Ctx)
		Ctx->ev = e;
}


#endif // CONFIG_SYSLOG
