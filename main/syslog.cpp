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
#include "binformats.h"
#include "event.h"
#include "globals.h"
#include "log.h"
#include "netsvc.h"
#include "shell.h"
#include "terminal.h"
#include "wifi.h"
#include "versions.h"

#include <string.h>

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
	Syslog()
	: mtx(xSemaphoreCreateMutex())
	{ }

	ip_addr_t addr;
	struct udp_pcb *pcb = 0;
	char *buf = 0;
	LogMsg *first = 0, *last = 0;
	uint16_t alloc = 0;
	uint16_t unsent = 0, overwr = 0;
	event_t ev = 0;
	SemaphoreHandle_t mtx;
};

static const char TAG[] = "rlog";
static Syslog *Ctx = 0;


static void syslog_start(void*);


static int sendmsg(LogMsg *m)
{
	if (Ctx->pcb == 0) {
		if (!wifi_station_isup())
			return 1;
		syslog_start(0);
		if (Ctx->pcb == 0)
			return 1;
	}
//	log_dbug(TAG,"sendmsg %p,%p",m,Ctx->pcb);
	const char *hn = Config.nodename().c_str();
	size_t hs = Config.nodename().size();
	int n;
	size_t s = m->ml+64;
	struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT, s, PBUF_RAM);
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
		e = udp_sendto(Ctx->pcb,pbuf,&Ctx->addr,SYSLOG_PORT);
		if (0 == e) {
			m->sent = true;
			--Ctx->unsent;
		} else {
			log_dbug(TAG,"sendto %d",e);
		}
	}
	pbuf_free(pbuf);
	return e;
}


static void sendall(void * = 0)
{
	if (Ctx->pcb == 0) {
//		log_dbug(TAG,"no pcb");
		return;
	}
#ifdef CONFIG_IDF_TARGET_ESP32
	if ((Ctx->addr.u_addr.ip4.addr == 0) || (Ctx->unsent == 0))
#else
	if ((Ctx->addr.addr == 0) || (Ctx->unsent == 0))
#endif
	{
//		log_dbug(TAG,"no IP");
		return;
	}
	unsigned x = 0;
	xSemaphoreTake(Ctx->mtx,portMAX_DELAY);
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
#ifdef CONFIG_IDF_TARGET_ESP8266
//			vTaskDelay(2);	// needed for the UDP transmission
#endif
		}
		m = m->next;
	}
	xSemaphoreGive(Ctx->mtx);
	log_dbug(TAG,"sent %u log messages",x);
}


static void syslog_start(void*)
{
	const char *host = Config.syslog_host().c_str();
	log_dbug(TAG,"host %s",host);
	uint32_t ip4 = resolve_hostname(host);
#ifdef CONFIG_IDF_TARGET_ESP32
	Ctx->addr.u_addr.ip4.addr = ip4;
	Ctx->addr.type = IPADDR_TYPE_V4;
	if ((Ctx->addr.u_addr.ip4.addr == IPADDR_NONE) || (Ctx->addr.u_addr.ip4.addr == 0))
#else
	Ctx->addr.addr = ip4;
	if ((Ctx->addr.addr == IPADDR_NONE) || (Ctx->addr.addr == 0))
#endif
	{
		log_dbug(TAG,"error resolving: %s",host);
		return;
	}
	log_dbug(TAG,"IP %d.%d.%d.%d",ip4&0xff,(ip4>>8)&0xff,(ip4>>16)&0xff,(ip4>>24)&0xff);
	Lock lock(Ctx->mtx);
	if (Ctx->pcb)
		udp_remove(Ctx->pcb);
	Ctx->pcb = udp_new();
	ip_addr_t ip;
	ip = *IP4_ADDR_ANY;
#ifdef CONFIG_IDF_TARGET_ESP32
	ip.type = IPADDR_TYPE_V4;
#endif
	if (err_t e = udp_bind(Ctx->pcb,&ip,0))	// 0: sending port can be any port
		log_warn(TAG,"udp_bind %d",e);
	event_trigger(Ctx->ev);
}

extern "C"
void log_syslog(log_level_t lvl, const char *a, const char *msg, size_t ml)
{
	// header: pri version timestamp hostname app-name procid msgid
	if ((Ctx == 0) || (a == TAG))
		return;
	struct timeval tv;
	tv.tv_sec = 0;
	gettimeofday(&tv,0);
	xSemaphoreTake(Ctx->mtx,portMAX_DELAY);
	LogMsg *m;
	if (Ctx->alloc + 128 > Config.dmesg_size()) {
		if (Ctx->first == Ctx->last) {
			m = Ctx->first;
		} else {
			m = Ctx->first;
			Ctx->first = m->next;
			Ctx->last->next = m;
			Ctx->last = m;
			Ctx->alloc -= m->ml;
			m->msg = (char*)realloc(m->msg,ml);
			if (!m->sent) {
				--Ctx->unsent;
				++Ctx->overwr;
			}
		}
	} else {
		m = new LogMsg;
		m->msg = (char*)malloc(ml);
		if (Ctx->last) {
			Ctx->last->next = m;
			Ctx->last = m;
		} else {
			Ctx->first = m;
			Ctx->last = m;
		}
		Ctx->alloc += sizeof(LogMsg);
	}
	m->lvl = lvl;
	m->ml = ml;
	m->next = 0;
	Ctx->alloc += ml;
	m->a = a;
	memcpy(m->msg,msg,ml);
	m->ntp = tv.tv_sec > 10000000;
	if (m->ntp)
		m->ts = tv.tv_sec;
	else
		m->ts = tv.tv_sec * 1000 + tv.tv_usec/1000;
	m->sent = false;
	++Ctx->unsent;
	event_trigger(Ctx->ev);
	xSemaphoreGive(Ctx->mtx);
}


int dmesg(Terminal &term, int argc, const char *args[])
{
	if (argc > 2) {
		term.printf("invalid number of arguments\n");
		return 1;
	}
	if (argc == 2) {
		char *eptr;
		long l = strtol(args[1],&eptr,0);
		if ((eptr == args[1]) || (l < 0)) {
			return arg_invalid(term,args[1]);
		}
		if (((l > 0) && (l < 512)) || (l > UINT16_MAX))
			return arg_invalid(term,args[1]);
		Config.set_dmesg_size(l);
		return 0;
	}
	if (Ctx == 0) {
		term.printf("dmesg is inactive\n");
		return 1;
	}
	Lock lock(Ctx->mtx);
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


int dmesg_setup()
{
	if (Config.dmesg_size() != 0) {
		if (Ctx == 0)
			Ctx = new Syslog;
	}
	return 0;
}


int syslog_setup(void)
{
	if (Ctx->ev == 0) {
		action_add("syslog!start",syslog_start,0,"start syslog");
		event_callback("wifi`station_up","syslog!start");
		Action *a = action_add("syslog!send",sendall,0,"start syslog");
		Ctx->ev = event_register("syslog`send");
		event_callback(Ctx->ev,a);
	}
	return 0;
}


#endif // CONFIG_SYSLOG
