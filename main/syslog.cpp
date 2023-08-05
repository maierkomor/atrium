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
//#define log_devel(tag,msg,...) log_local(tag,msg,__VA_ARGS__)
#else
#define log_devel(...)
#endif


enum { ntp_flag=(1<<3), sent_flag = (1<<4), sending_flag = (1<<5) };

struct LogMsg
{
	uint32_t ts;
	logmod_t mod;
	uint8_t ml;
	uint8_t flags;	// 0..2 log_level_t, 3: ntp, 4: sent
	char msg[LOG_MAXLEN];	// not 0-terminated
};

struct Syslog
{
	explicit Syslog(size_t s);
	~Syslog();

	struct udp_pcb *pcb = 0;
	LogMsg *msgs = 0;
	uint64_t ntpbase = 0;
	uint32_t sent = 0;
	int16_t unsent = 0, overwr = 0, lost = 0;
	uint16_t at = 0, num;
	event_t ev;
	bool triggered = false;
	LogMsg *create_msg();
	void resize(size_t);

	private:
	Syslog(const Syslog &);
	Syslog& operator = (const Syslog &);
	void shrink();
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
	num = s / sizeof(LogMsg);
	msgs = (LogMsg *) calloc(num,sizeof(LogMsg));
}


Syslog::~Syslog()
{
	if (pcb) {
		LWIP_LOCK();
		udp_remove(pcb);
		LWIP_UNLOCK();
	}
}


void Syslog::shrink()
{
	--num;
	if (at != num)
		memmove(msgs+at,msgs+at+1,sizeof(LogMsg)*(num-at));
	msgs = (LogMsg *)realloc(msgs,num*sizeof(LogMsg));
}


void Syslog::resize(size_t s)
{
	unsigned n = (s-sizeof(Syslog))/sizeof(LogMsg);
//	log_local(TAG,"resize (%u-%u)/%u=%u, using %u",s,sizeof(Syslog),sizeof(LogMsg),s/sizeof(LogMsg),sizeof(Syslog)+n*sizeof(LogMsg));
	if (n < num) {
		do {
			shrink();
		} while (n < num);
	} else if (n > num) {
		int delta = n - num;
		msgs = (LogMsg *)realloc(msgs, n * sizeof(LogMsg));
		bzero(msgs+num,delta*sizeof(LogMsg));
	}
	num = n;
}


static int sendmsg(LogMsg *m)
{
	if (m->msg[0] == 0)
		return 0;
	int n;
	char header[64];
	const char *mod = ModNames+ModNameOff[m->mod];
	uint8_t lvl = (m->flags&7)+3;
	if (m->flags&ntp_flag) {
		struct tm tm;
		time_t ts = m->ts/1000 + Ctx->ntpbase;
		gmtime_r(&ts,&tm);
		n = snprintf(header,sizeof(header),"<%d>1 %4u-%02u-%02uT%02u:%02u:%02u.%03uZ %.*s %s - - - "
			, 16 << 3 | lvl	// facility local use = 16, pri = (facility)<<3|serverity
			, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday
			, tm.tm_hour, tm.tm_min, tm.tm_sec, (unsigned)(m->ts%1000)
			, HostnameLen
			, Hostname
			, mod
			);
	} else {
		n = snprintf(header,sizeof(header),"<%d>1 - %.*s %s - - - "
			, 16 << 3 | lvl	// facility local use = 16, pri = (facility)<<3|serverity
			, HostnameLen
			, Hostname
			, mod
			);
	}
	assert(n < sizeof(header));
	log_devel(TAG,"send %.*s",n,header);
	LWIP_LOCK();	//-- this caused a hang - why? still true?
	struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, n+m->ml, PBUF_RAM);
	pbuf_take(pbuf,header,n);
	pbuf_take_at(pbuf,m->msg,m->ml,n);
	err_t e = udp_send(Ctx->pcb,pbuf);
	pbuf_free(pbuf);
	if (e) {
		udp_remove(Ctx->pcb);
		Ctx->pcb = 0;
		log_local(TAG,"sendto %d",e);
	}
	LWIP_UNLOCK();
	return e;
}


void syslog_stop()
{
	if (Ctx) {
		if (struct udp_pcb *pcb = Ctx->pcb) {
			Ctx->pcb = 0;
			udp_remove(pcb);
		}
	}
}


void sendall(void * = 0)
{
	if (Ctx == 0)
		return;
	if (Ctx->pcb == 0) {
		log_local(TAG,"no pcb");
		syslog_start(0);	// hangs on startup on IDF v4.x with station connect not finishing
		return;
	}
	unsigned x = 0;
	MLock lock(Mtx);
	LogMsg *at = Ctx->msgs+Ctx->at;
	LogMsg *end = Ctx->msgs+Ctx->num;
	LogMsg *m = at;
	do {
		if (m->msg[0] && (m->flags & sent_flag) == 0) {
			m->flags |= sending_flag;
			lock.unlock();
			int r = sendmsg(m);
			m->flags &= ~sending_flag;
			if (r) {
				event_trigger_nd(Ctx->ev);
#if CONFIG_IDF_TARGET_ESP32S2
				if (x) {
					vTaskDelay(10);	// needed for UDP stack to catch up
					lock.lock();
					continue;
				}
#endif
				goto done;
			}
			lock.lock();
			m->flags |= sent_flag;
			assert(Ctx->unsent>0);
			--Ctx->unsent;
			++Ctx->sent;
			++x;
		}
		++m;
		if (m == end)
			m = Ctx->msgs;
	} while (m != at);
	Ctx->triggered = false;
	lock.unlock();
done:
	log_local(TAG,"sent %u messages",x);
}


static void syslog_hostip(const char *hn, const ip_addr_t *ip, void *arg)
{
	// no LWIP_LOCK as called from tcpip_task
	if ((ip == 0) || (Ctx->pcb))
		return;
	if (Ctx->ev == 0)
		Ctx->ev = event_id("syslog`msg");
	err_t e;
	{
		Lock lock(Mtx,__FUNCTION__);
		udp_pcb *pcb;
		if (Ctx->pcb) {
			pcb = Ctx->pcb;
			Ctx->pcb = 0;
			udp_remove(pcb);
		}
		pcb = udp_new();
		e = udp_connect(pcb,ip,SYSLOG_PORT);
		if (e) {
			udp_remove(pcb);
		} else {
			Ctx->pcb = pcb;
			event_trigger_nd(Ctx->ev);
		}
	}
	if (e) {
		log_warn(TAG,"connect %s: %s",hn,strlwiperr(e));
	} else {
		log_local(TAG,"connected to %s",hn);
	}
}


static void syslog_start(void*)
{
	log_devel(TAG,"Ctx=%p,mode=%d,%d",Ctx,StationMode,Config.has_syslog_host());
	if ((Ctx == 0) || (StationMode != station_connected))
		return;
	if (!Config.has_syslog_host())
		return;
	const char *host = Config.syslog_host().c_str();
	err_t e = query_host(host,0,syslog_hostip,0);
	if (e < 0)
		log_warn(TAG,"query %s: %d",host,e);
	else
		log_local(TAG,"start");
}


LogMsg *Syslog::create_msg()
{
	// assume lock is alread taken
	LogMsg *r = msgs+at;
	if (r->flags & sending_flag) {
		++lost;
		return 0;
	}
	if (r->msg[0] && ((r->flags & sent_flag) == 0))
		++overwr;
	++at;
	at %= num;
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
		abort_on_mutex(Mtx,__BASE_FILE__);
	bool trigger = false;
	if (LogMsg *m = Ctx->create_msg()) {
		m->flags = lvl;
		m->ml = ml;
		m->mod = module;
		memcpy(m->msg,msg,ml);
		m->msg[ml] = 0;
		struct timeval tv2;
		if (tv == 0) {
			gettimeofday(&tv2,0);
			tv = &tv2;
		}
		if (tv->tv_sec > 10000000) {
			m->flags |= ntp_flag;
			if (Ctx->ntpbase == 0)
				Ctx->ntpbase = tv->tv_sec;
		}
		m->ts = (tv->tv_sec-Ctx->ntpbase) * 1000 + tv->tv_usec/1000;
		if (lvl != ll_local) {
			++Ctx->unsent;
			trigger = !Ctx->triggered;
			if (trigger)
				Ctx->triggered = trigger;
		} else {
			m->flags |= sent_flag;
		}
	}
	xSemaphoreGive(Mtx);
	if (trigger)
		event_trigger_nd(Ctx->ev);
}


const char *dmesg(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return "Invalid number of arguments.";;
	if (argc == 2) {
		char *eptr;
		long l = strtol(args[1],&eptr,0);
		if ((eptr == args[1]) || (l < 0)) {
			return "Invalid argument #1.";
		}
		if (((l > 0) && (l < 512)) || (l > UINT16_MAX))
			return "Invalid argument #1.";
		Config.set_dmesg_size(l);
		dmesg_resize(l);
		return 0;
	}
	if (Ctx == 0) {
		return "dmesg is inactive";
	}
	Lock lock(Mtx,__FUNCTION__);		// would need a recursive lock to support execution with debug on lwtcp
	LogMsg *at = Ctx->msgs+Ctx->at;
	LogMsg *m = at;
	term.printf("%u queued, %u overwritten, %u sent, %u lost\n",Ctx->unsent,Ctx->overwr,Ctx->sent,Ctx->lost);
	do {
		if (m->msg[0]) {
			const char *mod = ModNames+ModNameOff[m->mod];
			const char *lvls = "EWIDL";
			char status = (m->flags & sending_flag) ? 's' : (m->flags & sent_flag) ? ' ' : '*';
			if (m->flags & ntp_flag) {
				struct tm tm;
				time_t ts = m->ts/1000+Ctx->ntpbase;
				gmtime_r(&ts,&tm);
				term.printf("%c %c %02u:%02u:%02u.%03lu %-8s: "
					, status
					, *(lvls+(m->flags&7))
					, tm.tm_hour, tm.tm_min, tm.tm_sec, m->ts%1000
					, mod
					);
			} else {
				term.printf("%c %c %8u.%03lu %-8s: "
					, status
					, *(lvls+(m->flags&7))
					, m->ts/1000, m->ts%1000
					, mod
					);
			}
			term.write(m->msg,m->ml);
			term.println();
		}
		++m;
		if (m == Ctx->msgs+Ctx->num)
			m = Ctx->msgs;
	} while (m != at);
	return 0;
}


void dmesg_setup()
{
	Mtx = xSemaphoreCreateMutex();
	event_register("syslog`msg");
}


void dmesg_resize(size_t ds)
{
	Lock lock(Mtx,__FUNCTION__);
	if (Ctx == 0) {
		if (ds)
			Ctx = new Syslog(ds);
	} else {
		if (ds) {
			Ctx->resize(ds);
		} else {
			delete Ctx;
			Ctx = 0;
		}
	}
}


void syslog_setup(void)
{
	log_local(TAG,"setup");
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
