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
#include <lwip/udp.h>
#include <lwip/dns.h>
#include <lwip/pbuf.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define SYSLOG_PORT 514
#define PBUF_SIZE 256
#define MAX_FRAME_SIZE 1472

struct LogMsg
{
	LogMsg *next;
	uint32_t ts;
	const char *a;
	char *msg;
	uint8_t ml;
	log_level_t lvl;
	bool ntp;			// true: sec sind 1970, false msec since start
	bool sent;
};

struct Syslog
{
	explicit Syslog(size_t s);
	~Syslog();

	struct udp_pcb *pcb = 0;
	LogMsg *first = 0, *last = 0;
	uint16_t maxsize, alloc = 0;
	uint16_t unsent = 0, overwr = 0;
	event_t ev = 0;
	bool triggered = false;

	private:
	Syslog(const Syslog &);
	Syslog& operator = (const Syslog &);
};

static const char TAG[] = "rlog";
static SemaphoreHandle_t Mtx = 0;
static Syslog *Ctx = 0;


static void syslog_start(void*);


Syslog::Syslog(size_t s)
: maxsize(s)
{

}


Syslog::~Syslog()
{
	if (pcb)
		udp_remove(pcb);
	LogMsg *m = first;
	while (m) {
		LogMsg * n = m->next;
		delete m;
		m = n;
	}
}


static int sendmsg(LogMsg *m)
{
	if (Ctx->pcb == 0) {
		if (!wifi_station_isup())
			return 1;
		syslog_start(0);
		if (Ctx->pcb == 0)
			return 1;
	}
	const char *hn = Config.nodename().c_str();
	size_t hs = Config.nodename().size();
	int n;
	size_t s = m->ml+64;
	LOCK_TCPIP_CORE();
	struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, s, PBUF_RAM);
	if (pbuf == 0) {
		con_print("no pbuf - out of memory?");
		UNLOCK_TCPIP_CORE();
		return 1;
	}
	if (m->ntp) {
		struct tm tm;
		time_t ts = m->ts;
		gmtime_r(&ts,&tm);
		n = snprintf((char*)pbuf->payload,s,"<%d>1 %4u-%02u-%02uT%02u:%02u:%02u.%03u %.*s %s - - - %.*s"
			, 16 << 3 | (m->lvl+3)	// facility local use = 16, pri = (facility)<<3|serverity
			, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday
			, tm.tm_hour, tm.tm_min, tm.tm_sec, m->ts%1000
			, hs
			, hn
			, m->a
			, m->ml
			, m->msg
			);
	} else {
		n = snprintf((char*)pbuf->payload,s,"<%d>1 - %.*s %s - - - %.*s"
			, 16 << 3 | (m->lvl+3)	// facility local use = 16, pri = (facility)<<3|serverity
			, hs
			, hn
			, m->a
			, m->ml
			, m->msg
			);
	}
	if (n > s) {
		// pbuf too small
		n = m->ml+64;
	}
	err_t e = 0;
	if (n > 0) {
		pbuf->tot_len = n;
		pbuf->len = n;
		e = udp_send(Ctx->pcb,pbuf);
		if (0 == e) {
			m->sent = true;
			--Ctx->unsent;
		} else {
			log_dbug(TAG,"sendto %d",e);
		}
	}
	pbuf_free(pbuf);
	UNLOCK_TCPIP_CORE();
	return e;
}


static void sendall(void * = 0)
{
	if (Ctx == 0)
		return;
	if (Ctx->pcb == 0) {
//		log_dbug(TAG,"no pcb");
		return;
	}
	unsigned x = 0;
	xSemaphoreTake(Mtx,portMAX_DELAY);
	LogMsg *m = Ctx->first;
	while (m) {
		if (!m->sent) {
			if (0 != sendmsg(m)) {
				if (x) {
					vTaskDelay(1);	// needed for UDP stack to catch up
					continue;
				} else
					break;
			}
			++x;
		}
		m = m->next;
	}
	Ctx->triggered = false;
	xSemaphoreGive(Mtx);
	log_dbug(TAG,"sent %u log messages",x);
}


static void syslog_start(void*)
{
	if (Ctx == 0)
		return;
	const char *host = Config.syslog_host().c_str();
	log_dbug(TAG,"host %s",host);
	uint32_t ip4 = resolve_hostname(host);
	if (ip4 == 0) {
		log_dbug(TAG,"error resolving: %s",host);
		return;
	}
	log_dbug(TAG,"IP %d.%d.%d.%d",ip4&0xff,(ip4>>8)&0xff,(ip4>>16)&0xff,(ip4>>24)&0xff);
	Lock lock(Mtx);
	if (Ctx->pcb) {
		udp_remove(Ctx->pcb);
		Ctx->pcb = 0;
	}
	udp_pcb *pcb = udp_new();
	ip_addr_t ip;
#ifdef CONFIG_IDF_TARGET_ESP32
	ip.type = IPADDR_TYPE_V4;
#else
	ip.addr = ip4;
#endif
	if (err_t e = udp_connect(pcb,&ip,SYSLOG_PORT)) {
		log_warn(TAG,"udp_connect %d",e);
		return;
	}
	Ctx->pcb = pcb;
	if (Ctx->ev == 0)
		Ctx->ev = event_id("syslog`msg");
	if (Ctx->ev != 0)
		event_trigger(Ctx->ev);
}


static void shrink_dmesg(uint16_t ns)
{
	// assume lock is alread taken
	while ((Ctx->alloc > ns) && Ctx->first) {
		LogMsg *r = Ctx->first;
		if (r->next == 0) {
			Ctx->first = 0;
			Ctx->last = 0;
		} else {
			Ctx->first = r->next;
		}
		free(r->msg);
		Ctx->alloc -= r->ml;
		Ctx->alloc -= sizeof(LogMsg);
		if (!r->sent)
			--Ctx->unsent;
		delete r;
	}
}


extern "C"
void log_syslog(log_level_t lvl, const char *a, const char *msg, size_t ml)
{
	// header: pri version timestamp hostname app-name procid msgid
	if ((Ctx == 0) || (a == TAG))
		return;
	if ((ml<<2) > Ctx->maxsize)
		return;
	struct timeval tv;
	tv.tv_sec = 0;
	gettimeofday(&tv,0);
	Lock lock(Mtx);
	LogMsg *m;
	if (Ctx->alloc + ml + sizeof(LogMsg) > Ctx->maxsize) {
		shrink_dmesg(Ctx->maxsize-ml-sizeof(LogMsg));
		m = Ctx->first;
		if (m == Ctx->last) {
			Ctx->first = 0;
			Ctx->last = 0;
		} else {
			Ctx->first = m->next;
		}
		Ctx->alloc -= m->ml;
		if (!m->sent) {
			--Ctx->unsent;
			++Ctx->overwr;
		}
	} else {
		m = new LogMsg;
		m->msg = 0;
		Ctx->alloc += sizeof(LogMsg);
	}
	char *buf = (char*)realloc(m->msg,ml);
	if (buf == 0) {
		free(m->msg);
		delete m;
		con_print("syslog: out of memory");
		return;
	}
	m->msg = buf;
	if (Ctx->last) {
		Ctx->last->next = m;
		Ctx->last = m;
	} else {
		Ctx->first = m;
		Ctx->last = m;
	}
	Ctx->alloc += ml;
	m->lvl = lvl;
	m->ml = ml;
	m->next = 0;
	m->a = a;
	memcpy(m->msg,msg,ml);
	m->ntp = tv.tv_sec > 10000000;
	if (m->ntp)
		m->ts = tv.tv_sec;
	else
		m->ts = tv.tv_sec * 1000 + tv.tv_usec/1000;
	m->sent = false;
	++Ctx->unsent;
	if (!Ctx->triggered && (Ctx->ev != 0)) {
		event_trigger_nd(Ctx->ev);
		Ctx->triggered = true;
	}
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
		dmesg_resize();
		if ((l != 0) && wifi_station_isup())
			syslog_start(0);
		return 0;
	}
	if (Ctx == 0) {
		term.printf("dmesg is inactive\n");
		return 1;
	}
	Lock lock(Mtx);
	LogMsg *m = Ctx->first;
	term.printf("%u bytes, %u queued, %u overwritten\n",Ctx->alloc,Ctx->unsent,Ctx->overwr);
	while (m) {
		if (m->ntp) {
			struct tm tm;
			time_t ts = m->ts;
			gmtime_r(&ts,&tm);
			term.printf("%c %02u:%02u:%02u.%03lu %-8s: %.*s\n"
				, m->sent ? ' ' : '*'
				, tm.tm_hour, tm.tm_min, tm.tm_sec, m->ts%1000
				, m->a
				, m->ml
				, m->msg
				);
		} else {
			term.printf("%c %8u.%03lu %-8s: %.*s\n"
				, m->sent ? ' ' : '*'
				, m->ts/1000, m->ts%1000
				, m->a
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
}


void dmesg_resize()
{
	size_t ds = Config.dmesg_size();
	if (Ctx) {
		Lock lock(Mtx);
		if (ds == 0) {
			delete Ctx;
			Ctx = 0;
		} else {
			if (ds < Ctx->maxsize)
				shrink_dmesg(ds);
			Ctx->maxsize = ds;
		}
	} else if (ds) {
		Ctx = new Syslog(ds);
	}
}


int syslog_setup(void)
{
	action_add("syslog!start",syslog_start,0,"start syslog");
	event_callback("wifi`station_up","syslog!start");
	Action *a = action_add("syslog!send",sendall,0,"start syslog");
	event_t e = event_register("syslog`msg");
	event_callback(e,a);
	if (Ctx)
		Ctx->ev = e;
	return 0;
}


#endif // CONFIG_SYSLOG
