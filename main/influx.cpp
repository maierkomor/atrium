/*
 *  Copyright (C) 2020-2024, Thomas Maier-Komor
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

#if 0
#define log_devel log_dbug
#else
#define log_devel(...)
#endif

using namespace std;

#define TAG MODULE_INFLUX

typedef enum state {
	offline = 0, connecting, running, error, stopped, term, bug
} state_t;

static const char *States[] = {
	"offline", "connecting", "running", "error", "stopped", "terminate", "bug"
};

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
	log_devel(TAG,"sent %p %u",pcb,l);
	return 0;
}


static err_t handle_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pbuf, err_t e)
{
	log_devel(TAG,"recv %u err=%d",pbuf ? pbuf->tot_len : 0,e);
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
	log_dbug(TAG,"terminating");
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
#endif
	if (struct udp_pcb *pcb = UPCB) {
		UPCB = 0;
		udp_remove(pcb);
	}
	if (struct tcp_pcb *pcb = TPCB) {
		TPCB = 0;
		tcp_close(pcb);
	}
	if (term == State)
		State = stopped;
	else if (stopped != State)
		State = offline;
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_UNLOCK();
#else
	xSemaphoreGive(LwipSem);
#endif
	log_info(TAG,"terminated");
}


static void influx_term(void * = 0)
{
	Lock lock(Mtx,__FUNCTION__);
#ifdef CONFIG_IDF_TARGET_ESP8266
	term_fn(0);
#else
	tcpip_send_msg_wait_sem(term_fn,0,&LwipSem);
#endif
}


static void influx_init(void * = 0)
{
	if ((StationMode != station_connected) || !Config.has_influx() || !Config.has_nodename())
		return;
	const Influx &influx = Config.influx();
	if (!influx.has_hostname() || !influx.has_port() || !influx.has_measurement())
		return;
	log_dbug(TAG,"init");
	if (State == error) {
		influx_term();
	}
	if (State == offline) {
		const char *host = influx.hostname().c_str();
		int e = query_host(host,0,influx_connect,0);
		if (e < 0)
			log_warn(TAG,"query host: %d",e);
	} else {
		log_dbug(TAG,"no init, state %s",States[State]);
	}
}


static void influx_connect(const char *hn, const ip_addr_t *addr, void *arg)
{
	// connect is called from tcpip_task as callback
	if (0 == addr)
		return;
	if ((0 != UPCB) || (0 != TPCB)) {
		State = running;
		return;
	}
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
		if (Header) {
			free(Header);
			Header = 0;
		}
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
	char addrstr[32];
	inet_ntoa_r(*addr,addrstr,sizeof(addrstr));
	assert((0 == UPCB) && (0 == TPCB));
	log_info(TAG,"connect %s:%u",addrstr,port);
	err_t e;
	if (influx.database().empty()) {
		UPCB = udp_new();
		e = udp_connect(UPCB,addr,port);
		if (0 == e) {
			State = running;
		}
	} else {
		TPCB = tcp_new();
		tcp_err(TPCB,handle_err);
		e = tcp_connect(TPCB,addr,port,handle_connect);
		if (0 == e) {
			State = connecting;
			astream str(128,true);
			const auto &i = Config.influx();
			str <<	"POST /write?db=" << i.database() << " HTTP/1.1\n"
				"Connection: keep-alive\n"
				"Content-Type: application/x-www-form-urlencoded\n"
				"Host: " << i.hostname() << ':' << i.port() << "\n"
				"Content-Length: ";
			if (TcpHdr)
				free(TcpHdr);
			THL = str.size();
			TcpHdr = str.take();
		}
	}
	if (e) {
		log_warn(TAG,"connect: %s",strlwiperr(e));
		State = error;
	}
}


static int influx_send_check()
{
	switch (State) {
	case offline:
		influx_init();
		return 1;
	case stopped:
	case connecting:
	case bug:
		return 1;
	case term:
	case error:
		influx_term();
		return 1;
	case running:
		return 0;
	default:
		break;
	}
	return 1;
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
	if (influx_send_check())
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
	void *sp = memchr(s->data,' ',s->len);
	if ((sp == 0) || (0 == memchr(sp,' ',s->len-((char*)sp-(char*)s->data)))) {
		State = bug;
		log_warn(TAG,"BUG: %.*s",s->len,s->data);
		xSemaphoreGive(LwipSem);
		return;
	}
	log_dbug(TAG,"send '%.*s'",s->len,s->data);
	err_t e;
	if (TPCB) {
		char len[16];
		int n = sprintf(len,"%u\r\n\r\n",s->len);
		e = tcp_write(TPCB,TcpHdr,THL,TCP_WRITE_FLAG_MORE);
		if (e == 0)
			e = tcp_write(TPCB,len,n,TCP_WRITE_FLAG_MORE|TCP_WRITE_FLAG_COPY);
		if (e == 0)
			e = tcp_write(TPCB,s->data,s->len,TCP_WRITE_FLAG_COPY);
		if (e == 0)
			tcp_output(TPCB);
	} else if (UPCB) {
		struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT,s->len,PBUF_RAM);
		pbuf_take(pbuf,s->data,s->len);
		e = udp_send(UPCB,pbuf);
		pbuf_free(pbuf);
	} else {
		e = 0;
	}
	xSemaphoreGive(LwipSem);
	if (e) {
		State = error;
		log_warn(TAG,"send: %s",strlwiperr(e));
	}
}
#endif


int influx_send(const char *data, size_t l)
{
	Lock lock(Mtx,__FUNCTION__);
#ifdef CONFIG_IDF_TARGET_ESP8266
	err_t e;
	log_dbug(TAG,"send '%.*s'",l,data);
	LWIP_LOCK();
	if (TPCB) {
		char len[16];
		int n = sprintf(len,"%u\r\n\r\n",l);
		e = tcp_write(TPCB,TcpHdr,THL,TCP_WRITE_FLAG_MORE);
		if (e == 0)
			e = tcp_write(TPCB,len,n,TCP_WRITE_FLAG_MORE|TCP_WRITE_FLAG_COPY);
		if (e == 0)
			e = tcp_write(TPCB,data,l,TCP_WRITE_FLAG_COPY);
		if (e == 0)
			tcp_output(TPCB);
	} else if (UPCB) {
		struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT,l,PBUF_RAM);
		pbuf_take(pbuf,data,l);
		e = udp_send(UPCB,pbuf);
		pbuf_free(pbuf);
	} else {
		e = 0;
	}
	LWIP_UNLOCK();
	if (e) {
		State = error;
		log_warn(TAG,"send: %s",strlwiperr(e));
	}
#else
	send_t a;
	a.data = data;
	a.len = l;
	tcpip_send_msg_wait_sem(send_fn,&a,&LwipSem);
#endif
	return 0;
}


static void add_element(EnvNumber *e, stream &str, char extra)
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
			add_element(n,str,'_');
			comma = ',';
		}
	}
	return comma;
}


static void send_rtdata(void *)
{
	if (influx_send_check())
		return;
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
				add_element(n,str,comma);
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
static uint8_t LNT = 0;

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
	}
	if (LSt)
		free(LSt);
	LSt = st;
	LNT = nt;
}
#endif


static void send_sys_info(void *)
{
	if (influx_send_check())
		return;
	astream str;
	str.write(Header,HL);
	str.printf(" mem32=%u,mem8=%u,memd=%u"
		, heap_caps_get_free_size(MALLOC_CAP_32BIT)
		, heap_caps_get_free_size(MALLOC_CAP_8BIT)
		, heap_caps_get_free_size(MALLOC_CAP_DMA));
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
	action_add("influx!sysinfo",send_sys_info,0,"send system info");
	action_add("influx!rtdata",send_rtdata,0,"send runtime data");
	action_add("influx!init",influx_init,0,"init influx connection");
	action_add("influx!term",influx_term,0,"term influx connection");
	log_info(TAG,"setup");
}


const char *influx(Terminal &t, int argc, const char *args[])
{
	if (argc > 3)
		return "Invalid number of arguments.";
	if (argc == 1) {
		if (Config.has_influx()) {
			const Influx &i = Config.influx();
			if (i.has_hostname())
				t.printf("hostname   : %s\n",i.hostname().c_str());
			if (i.has_port())
				t.printf("port       : %u (%s)\n",i.port(),i.has_database()?"TCP":"UDP");
			if (i.has_database())
				t.printf("database   : %s\n",i.database().c_str());
			if (i.has_measurement())
				t.printf("measurement: %s\n",i.measurement().c_str());
			if (Header)
				t.printf("header     : '%s'\n",Header);
			const char *mode;
			if (State == running) {
				if (UPCB)
					mode = "UDP ready";
				else if (TPCB)
					mode = "TCP connected";
				else
					mode = "missing PCB";
			} else {
				mode = States[State];
			}
			t.println(mode);
		} else {
			return "Not configured.";
		}
	} else if (argc == 2) {
		if (!strcmp("init",args[1])) {
			action_dispatch("influx!init",0);
		} else if (0 == strcmp(args[1],"clear")) {
			Config.clear_influx();
		} else if (0 == strcmp(args[1],"stop")) {
			State = stopped;
		} else if (0 == strcmp(args[1],"start")) {
			if (State == stopped)
				State = offline;
		} else if (0 == strcmp(args[1],"term")) {
			State = term;
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
				t.printf("'%c' missing\n",':');
				return "";
			}
			const char *s = strchr(c+1,'/');
			if (s == 0) {
				t.printf("'%c' missing\n",'/');
				return "";
			}
			long l = strtol(c+1,0,0);
			if ((l <= 0) || (l >= UINT16_MAX))
				return "Value out of range.";
			i->set_hostname(args[2],c-args[2]);
			i->set_port(l);
			i->set_measurement(s+1);
			return 0;
		} else if (0 == strcmp(args[1],"db")) {
			return i->setByName("database",args[2]) < 0 ? "Failed." : 0;
		} else if (0 == strcmp(args[1],"mm")) {
			return i->setByName("measurement",args[2]) < 0 ? "Failed." : 0;
		}
		return i->setByName(args[1],args[2]) < 0 ? "Failed." : 0;
	}
	return 0;
}

#endif
