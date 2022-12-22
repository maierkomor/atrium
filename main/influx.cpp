/*
 *  Copyright (C) 2020-2022, Thomas Maier-Komor
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

#ifdef CONFIG_INFLUX

#include "actions.h"
#include "astream.h"
#include "globals.h"
#include "influx.h"
#include "env.h"
#include "log.h"
#include "netsvc.h"
#include "swcfg.h"
#include "terminal.h"
#include "wifi.h"

#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/FreeRTOSConfig.h>
#include <freertos/task.h>

#include <lwip/tcpip.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/priv/tcpip_priv.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef write
#undef write
#endif

using namespace std;

#define TAG MODULE_INFLUX

typedef enum state {
	offline, connecting, running, error, stopped
} state_t;

static struct udp_pcb *UPCB = 0;
static struct tcp_pcb *TPCB = 0;
static char *TcpHdr = 0;
static char *Header = 0;
static uint16_t THL = 0;
static uint16_t HL = 0;
static state_t State = offline;
static SemaphoreHandle_t Mtx = 0;
#ifndef CONFIG_IDF_TARGET_ESP8266
static sys_sem_t LwipSem = 0;
#endif


static void handle_err(void *arg, err_t e)
{
	log_warn(TAG,"handle error %s",strlwiperr(e));
	if (e == ERR_ISCONN) {
		State = running;
	} else {
		State = error;
		TPCB = 0;
	}
}


static err_t handle_sent(void *arg, struct tcp_pcb *pcb, u16_t l)
{
//	log_dbug(TAG,"sent %p %u",pcb,l);
	return 0;
}


static err_t handle_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pbuf, err_t e)
{
//	log_dbug(TAG,"recv %u err=%d",pbuf ? pbuf->tot_len : 0,e);
	assert(pcb);
	if (e == 0) {
		if (pbuf && log_module_enabled(TAG)) {
			char *tmp = (char *) malloc(pbuf->tot_len + 1);
			tmp[pbuf->tot_len] = 0;
			pbuf_copy_partial(pbuf,tmp,pbuf->tot_len,0);
			log_dbug(TAG,"answer:\n%.*s",pbuf->tot_len,tmp);
			free(tmp);
		}
	} else {
		log_dbug(TAG,"recv error %d",e);
	}
	if (pbuf) {
		tcp_recved(pcb,pbuf->tot_len);
		pbuf_free(pbuf);
	}
	return 0;
}


static err_t handle_connect(void *arg, struct tcp_pcb *pcb, err_t x)
{
	assert(x == ERR_OK);	// according to LWIP docu
	tcp_recv(pcb,handle_recv);
	tcp_sent(pcb,handle_sent);
	State = running;
	log_info(TAG,"connected");
	return 0;
}


static void influx_connect(const char *hn, const ip_addr_t *addr, void *arg);


static inline void term_fn(void *)
{
	log_dbug(TAG,"stop_fn");
	LWIP_LOCK();
	if (struct udp_pcb *pcb = UPCB) {
		UPCB = 0;
		udp_remove(pcb);
	}
	if (struct tcp_pcb *pcb = TPCB) {
		log_info(TAG,"tearing down connection");
		TPCB = 0;
		tcp_close(pcb);
	}
	LWIP_UNLOCK();
	if (State != stopped)
		State = offline;
#ifndef CONFIG_IDF_TARGET_ESP8266
	xSemaphoreGive(LwipSem);
#endif
}


static void influx_term(void * = 0)
{
#ifdef CONFIG_IDF_TARGET_ESP8266
	term_fn(0);
#else
	tcpip_send_msg_wait_sem(term_fn,0,&LwipSem);
#endif
}


static void influx_init(void * = 0)
{
	log_dbug(TAG,"init");
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	if ((StationMode != station_connected) || !Config.has_influx() || !Config.has_nodename())
		return;
	const Influx &influx = Config.influx();
	if (!influx.has_hostname() || !influx.has_port() || !influx.has_measurement())
		return;
	if (State == error) {
		if (TPCB) {
			tcp_close(TPCB);
			TPCB = 0;
		}
	}
	if ((State == offline) || (State == error)) {
		const char *host = Config.influx().hostname().c_str();
		if (err_t e = query_host(host,0,influx_connect,0))
			log_warn(TAG,"query host: %d",e);
	}
}


static void influx_connect(const char *hn, const ip_addr_t *addr, void *arg)
{
	// connect is called from tcpip_task as callback
	Lock lock(Mtx,__FUNCTION__);
	if ((State == connecting) || (State == running)) {
		log_dbug(TAG,"invalid state %d",State);
		return;
	}
	const Influx &influx = Config.influx();
	uint16_t port = influx.port();
	size_t hl = influx.measurement().size();
	size_t nl = Config.nodename().size();	// 1 for trailing space of header
	if (nl)
		hl += 6 + nl;
	char *nh = (char*)realloc(Header,hl+1);			// 1 for \0
	if (nh == 0) {
		HL = 0;
		free(Header);
		Header = 0;
		log_error(TAG,"Out of memory.");
		return;
	}
	Header = nh;
	strcpy(Header,influx.measurement().c_str());
	if (nl) {
		strcat(Header,",node=");
		strcat(Header,Config.nodename().c_str());
	}
	Header[hl] = 0;
	HL = hl;
	if (TPCB) {
		tcp_close(TPCB);
		TPCB = 0;
	}
	char addrstr[32];
	inet_ntoa_r(*addr,addrstr,sizeof(addrstr));
	if (influx.database().empty()) {
		if (UPCB == 0) {
			UPCB = udp_new();
			if (err_t e = udp_connect(UPCB,addr,port)) {
				log_warn(TAG,"listen %s %d",addrstr,e);
			} else {
				log_info(TAG,"listen %s:%u",addrstr,port);
			}
			State = running;
		}
	} else {
		if (UPCB) {
			udp_remove(UPCB);
			UPCB = 0;
		}
		TPCB = tcp_new();
		State = connecting;
		tcp_err(TPCB,handle_err);
		if (err_t e = tcp_connect(TPCB,addr,port,handle_connect)) {
			log_warn(TAG,"connect: %s",strlwiperr(e));
			State = error;
		} else {
			log_info(TAG,"connect %s:%u",addrstr,port);
			astream str(128,true);
			const auto &i = Config.influx();
			str <<	"POST /write?db=" << i.database() << " HTTP/1.1\n"
				"Connection: keep-alive\n"
				"Content-Type: application/x-www-form-urlencoded\n"
				"Host: " << i.hostname() << ':' << i.port() << "\n"
				"Content-Length: ";
			free(TcpHdr);
			THL = str.size();
			TcpHdr = str.take();
		}
	}
}


int influx_header(char *h, size_t l)
{
	if (Header == 0)
		return 0;
	Lock lock(Mtx,__FUNCTION__);
	if (l > HL)
		memcpy(h,Header,HL+1);
	return HL;
}


void influx_sendf(const char *fmt, ...)
{
	if (Header == 0)
		return;
	char buf[128], *b;
	va_list val;
	size_t n;
	va_start(val,fmt);
	{
		Lock lock(Mtx,__FUNCTION__);
		n = vsnprintf(buf+HL,sizeof(buf)-HL,fmt,val);
		if ((n + HL) > sizeof(buf)) {
			b = (char *)malloc(n+1);
			vsprintf(b+HL,fmt,val);
		} else {
			b = buf;
		}
		memcpy(b,Header,HL);
	}
	va_end(val);
	influx_send(b,n);
	if (b != buf)
		free(b);
}


#ifndef CONFIG_IDF_TARGET_ESP8266
typedef struct send_s {
	const void *data;
	size_t len;
} send_t;

static void send_fn(void *arg)
{
	send_t *s = (send_t *)arg;
	if (TPCB) {
//		log_dbug(TAG,"tcp send '%.*s'",l,data);
		char len[16];
		int n = sprintf(len,"%u\r\n\r\n",s->len);
		if (err_t e = tcp_write(TPCB,TcpHdr,THL,TCP_WRITE_FLAG_MORE)) {
			log_warn(TAG,"send header: %s",strlwiperr(e));
			State = error;
		} else if (err_t e = tcp_write(TPCB,len,n,TCP_WRITE_FLAG_MORE|TCP_WRITE_FLAG_COPY)) {
			log_warn(TAG,"send length: %s",strlwiperr(e));
			State = error;
		} else if (err_t e = tcp_write(TPCB,s->data,s->len,TCP_WRITE_FLAG_COPY)) {
			log_warn(TAG,"send data: %s",strlwiperr(e));
			State = error;
		} else {
			tcp_output(TPCB);
		}
	} else if (UPCB) {
		struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT,s->len,PBUF_RAM);
		pbuf_take(pbuf,s->data,s->len);
		if (err_t e = udp_send(UPCB,pbuf))
			log_warn(TAG,"send failed: %d",e);
		pbuf_free(pbuf);
	}
	xSemaphoreGive(LwipSem);
}
#endif


int influx_send(const char *data, size_t l)
{
	if (State != running) {
		log_dbug(TAG,"send: not running");
		if (State != connecting)
			influx_init();
		return 0;
	}
	log_dbug(TAG,"send '%.*s'",l,data);
	Lock lock(Mtx,__FUNCTION__);
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	if (TPCB) {
//		log_dbug(TAG,"tcp send '%.*s'",l,data);
		char len[16];
		int n = sprintf(len,"%u\r\n\r\n",l);
		esp_err_t e;
		if (0 != (e = tcp_write(TPCB,TcpHdr,THL,TCP_WRITE_FLAG_MORE))) {
		} else if (0 != (e = tcp_write(TPCB,len,n,TCP_WRITE_FLAG_MORE|TCP_WRITE_FLAG_COPY))) {
		} else if (0 != (e = tcp_write(TPCB,data,l,TCP_WRITE_FLAG_COPY))) {
		} else {
			tcp_output(TPCB);
		}
		if (e) {
			log_warn(TAG,"send error: %s",strlwiperr(e));
			State = error;
		}
	} else if (UPCB) {
		struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT,l,PBUF_RAM);
		pbuf_take(pbuf,data,l);
		if (err_t e = udp_send(UPCB,pbuf))
			log_warn(TAG,"send error: %s",strlwiperr(e));
		pbuf_free(pbuf);
	}
	LWIP_UNLOCK();
#else
	send_t a;
	a.data = data;
	a.len = l;
	tcpip_send_msg_wait_sem(send_fn,&a,&LwipSem);
#endif
	return 0;
}


static void send_element(EnvNumber *e, stream &str, char extra)
{
	if (extra)
		str << extra;
	str << e->name();
	str << '=';
	e->writeValue(str);
}


static char send_elements(EnvObject *o, stream &str, char comma)
{
	const char *name = o->name();
	for (EnvElement *e : o->getChilds()) {
		EnvNumber *n = e->toNumber();
		if (n && n->isValid()) {
			str << comma;
			str << name;
			send_element(n,str,'_');
			comma = ',';
		}
	}
	return comma;
}


static void send_rtdata(void *)
{
	if (State != running) {
		log_dbug(TAG,"rtdata: invalid state %d",State);
		if (State != connecting) {
			influx_init();
		}
		return;
	}
	astream str;
	{
		Lock lock(Mtx,__FUNCTION__);
		str << Header;
	}
	rtd_lock();
	char comma = ' ';
	for (EnvElement *e : RTData->getChilds()) {
		if (EnvObject *o = e->toObject()) {
			comma = send_elements(o,str,comma);
		} else if (EnvNumber *n = e->toNumber()) {
			if (n->isValid()) {
				send_element(n,str,comma);
				comma = ',';
			}
		}
	}
	rtd_unlock();
	if (comma != ' ') {
		str << '\n';
		influx_send(str.buffer(),str.size());
	}
}


#if configUSE_TRACE_FACILITY == 1
static TaskStatus_t *LSt = 0;
static unsigned LNT = 0;

static void compare_task_sets(TaskStatus_t *st, unsigned nt, stream &s)
{
	unsigned dt[nt] = {0}, sum = 0;
	for (int i = 0; i < nt; ++i) {
		for (int j = 0; j < LNT; ++j) {
			if (st[i].xTaskNumber == LSt[j].xTaskNumber) {
				dt[i] = st[i].ulRunTimeCounter - LSt[j].ulRunTimeCounter;
				sum += dt[i];
				break;
			}
		}
	}
	for (int i = 0; i < nt; ++i) {
		s << ",task_";
		if (char *c = strchr(st[i].pcTaskName,' '))
			*c = '_';
		s << st[i].pcTaskName;
		s << '=';
		s << (float)dt[i] * 100.0 / (float)sum;
	}
}


// must be called with constant interval time
static void proc_mon(stream &s)
{
	unsigned nt = uxTaskGetNumberOfTasks();
	TaskStatus_t *st = (TaskStatus_t*) malloc(nt*sizeof(TaskStatus_t));
	if (st) {
		nt = uxTaskGetSystemState(st,nt,0);
		if (LNT > 0)
			compare_task_sets(st,nt,s);
	} else {
		log_error(TAG,"Out of memory.");
	}
	free(LSt);
	LSt = st;
	LNT = nt;
}
#endif


static void send_sys_info(void *)
{
	if (State != running) {
		log_dbug(TAG,"sysinfo: state %d",State);
		if (State != connecting)
			influx_init();
		return;
	}
	astream str;
	str.write(Header,HL);
	str.printf(" mem32=%u,mem8=%u,memd=%u"
		,heap_caps_get_free_size(MALLOC_CAP_32BIT)
		,heap_caps_get_free_size(MALLOC_CAP_8BIT)
		,heap_caps_get_free_size(MALLOC_CAP_DMA));
#if configUSE_TRACE_FACILITY == 1
	proc_mon(str);
#endif
	influx_send(str.buffer(),str.size());
}


void influx_setup()
{
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
#ifndef CONFIG_IDF_TARGET_ESP8266
	if (LwipSem == 0)
		LwipSem = xSemaphoreCreateBinary();
#endif
	action_add("influx!sysinfo",send_sys_info,0,"send system info to influx");
	action_add("influx!rtdata",send_rtdata,0,"send runtime data to influx");
	action_add("influx!init",influx_init,0,"init influx connection");
	action_add("influx!term",influx_term,0,"init influx connection");
	log_info(TAG,"setup");
}


const char *influx(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return "Invalid nunber of arguments.";
	if (argc == 1) {
		if (Config.has_influx()) {
			const Influx &i = Config.influx();
			if (i.has_hostname())
				term.printf("hostname   : %s\n",i.hostname().c_str());
			if (i.has_port())
				term.printf("port       : %u (%s)\n",i.port(),i.has_database()?"TCP":"UDP");
			if (i.has_database())
				term.printf("database   : %s\n",i.database().c_str());
			if (i.has_measurement())
				term.printf("measurement: %s\n",i.measurement().c_str());
			if (Header)
				term.printf("header     : '%s'\n",Header);
			if (State == running) {
				const char *mode = "";
				if (UPCB)
					mode = "UDP ready";
				else if (TPCB)
					mode = "TCP connected";
				term.println(mode);
			}
		} else {
			return "Not configured.";
		}
	} else if (argc == 2) {
		if (!strcmp("init",args[1])) {
			if (State == stopped)
				State = offline;
			return action_dispatch("influx!init",0) ? "Failed." : 0;
		} else if (0 == strcmp(args[1],"clear")) {
			Config.clear_influx();
		} else if (0 == strcmp(args[1],"stop")) {
			State = stopped;
			return 0;
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 3) {
		Influx *i = Config.mutable_influx();
		if (0 == strcmp(args[1],"clear"))
			return Config.mutable_influx()->setByName(args[2],0) ? "Failed." : 0;
		if (0 == strcmp(args[1],"config")) {
			const char *c = strchr(args[2],':');
			if (c == 0) {
				term.printf("'%c' missing\n",':');
				return "";
			}
			const char *s = strchr(c+1,'/');
			if (s == 0) {
				term.printf("'%c' missing\n",'/');
				return "";
			}
			long l = strtol(c+1,0,0);
			if ((l <= 0) || (l >= UINT16_MAX))
				return "Value out of range.";
			i->set_hostname(args[2],c-args[2]);
			i->set_port(l);
			i->set_measurement(s+1);
			return 0;
		}
		return i->setByName(args[1],args[2]) < 0 ? "Failed." : 0;
	}
	return 0;
}

#endif
