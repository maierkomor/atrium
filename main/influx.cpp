/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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
#include "binformats.h"
#include "dataflow.h"
#ifdef CONFIG_SIGNAL_PROC
#include "func.h"
#endif
#include "globals.h"
#include "influx.h"
#include "ujson.h"
#include "log.h"
#include "netsvc.h"
#include "shell.h"
#include "support.h"
#include "terminal.h"
#include "wifi.h"

#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/FreeRTOSConfig.h>
#include <freertos/task.h>

#include <lwip/tcpip.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef write
#undef write
#endif

using namespace std;

static const char TAG[] = "influx";

typedef enum state {
	offline, connecting, running, error
} state_t;

#ifdef SOCKAPI
static int Sock = -1;
static struct sockaddr_in Addr;
#else
static struct udp_pcb *UPCB = 0;
static struct tcp_pcb *TPCB = 0;
static state_t State = offline;
static char *TcpHdr = 0;
static uint16_t THL = 0;
#endif
static char *Header = 0;
static uint16_t HL = 0;
static SemaphoreHandle_t Mtx = 0;


#ifdef CONFIG_SIGNAL_PROC
class FnInfluxSend : public Function
{
	public:
	explicit FnInfluxSend(const char *name)
	: Function(name)
	, m_sig(0)
	{
		action_add(concat(name,"!send"),(void(*)(void*))sendout,this,"send current data");
	}

	void operator () (DataSignal *);
	int setParam(unsigned x, DataSignal *s);
	static void sendout(FnInfluxSend *);
	static const char FuncName[];

	private:
	vector<DataSignal *> m_sig;
};

const char FnInfluxSend::FuncName[] = "influx_send";

int FnInfluxSend::setParam(unsigned x, DataSignal *s)
{
	s->addFunction(this);
	m_sig.push_back(s);
	return 0;
}


void FnInfluxSend::sendout(FnInfluxSend *o)
{
	astream str(128);
	{
		Lock lock(Mtx);
		str.write(Header,HL);
	}
	unsigned count = 0;
	for (auto s : o->m_sig) {
		if (s->isValid()) {
			str << s->signalName();
			str << '=';
			s->toStream(str);
			str << ',';
			++count;
		}
	}
	log_dbug(TAG,"InfluxSend('%s')",str.buffer());
	if (count)
		influx_send(str.buffer(),str.size()-1);
}


void FnInfluxSend::operator () (DataSignal *s)
{
}
#endif //CONFIG_SIGNAL_PROC


static void handle_err(void *arg, err_t e)
{
	log_warn(TAG,"handle error %d",e);
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
		if (pbuf) {
			char *tmp = (char *) malloc(pbuf->tot_len + 1);
			tmp[pbuf->tot_len] = 0;
			pbuf_copy_partial(pbuf,tmp,pbuf->tot_len,0);
			log_dbug(TAG,"answer:\n%.*s",pbuf->tot_len,tmp);
			tcp_recved(pcb,pbuf->tot_len);
			free(tmp);
		}
	} else {
		log_dbug(TAG,"recv error %d",e);
	}
	if (pbuf)
		pbuf_free(pbuf);
	return 0;
}


static err_t handle_connect(void *arg, struct tcp_pcb *pcb, err_t x)
{
	assert(x == ERR_OK);
	log_dbug(TAG,"connect %u",tcp_sndbuf(pcb));
	tcp_recv(pcb,handle_recv);
	tcp_sent(pcb,handle_sent);
	State = running;
	return 0;
}


static int influx_init()
{
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	if (StationMode != station_connected)
		return 0;
	if (!Config.has_influx() || !Config.has_nodename())
		return 1;
	const Influx &influx = Config.influx();
	if (!influx.has_hostname() || !influx.has_port() || !influx.has_measurement())
		return 1;
	uint16_t port = influx.port();
	if (port == 0)
		return 1;
	uint32_t ip = resolve_hostname(influx.hostname().c_str());
	if ((IPADDR_NONE == ip) || (ip == 0)) {
		log_warn(TAG,"unknown host %s",influx.hostname().c_str());
		return 1;
	}
	log_dbug(TAG,"init");
	Lock lock(Mtx);
	if (State == connecting) {
		log_dbug(TAG,"init stopped, already connecting");
		return 1;
	}
	size_t hl = influx.measurement().size();
	size_t nl = Config.nodename().size();	// 1 for trailing space of header
	if (nl)
		hl += 7 + nl;
	Header = (char*)realloc(Header,hl+1);			// 1 for \0
	if (Header == 0) {
		HL = 0;
		log_warn(TAG,"out of memory");
		return 1;
	}
	strcpy(Header,influx.measurement().c_str());
	if (nl) {
		strcat(Header,",node=");
		strcat(Header,Config.nodename().c_str());
	}
	Header[hl-1] = ' ';
	Header[hl] = 0;
	HL = hl;
	ip_addr_t addr;
#ifdef CONFIG_IDF_TARGET_ESP8266
	addr.addr = ip;
#else
	addr.type = IPADDR_TYPE_V4;
	addr.u_addr.ip4.addr = ip;
#endif
	LOCK_TCPIP_CORE();
	if (influx.database().empty()) {
		if (UPCB == 0) {
			UPCB = udp_new();
			if (err_t e = udp_connect(UPCB,&addr,port))
				log_warn(TAG,"udp connect %d",e);
			else
				log_dbug(TAG,"udp connect %d.%d.%d.%d:%u"
						,ip&0xff
						,(ip>>8)&0xff
						,(ip>>16)&0xff
						,(ip>>24)&0xff
						,port);
			State = running;
		}
		if (TPCB != 0) {
			tcp_abort(TPCB);
			TPCB = 0;
		}
	} else {
		if (UPCB) {
			udp_remove(UPCB);
			UPCB = 0;
		}
		if (TPCB == 0)
			TPCB = tcp_new();
		State = connecting;
		tcp_err(TPCB,handle_err);
		err_t e = tcp_connect(TPCB,&addr,port,handle_connect);
		log_dbug(TAG,"tcp connect: %d",e);
		if (e) {
			State = error;
			return 1;
		}
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
	UNLOCK_TCPIP_CORE();
	log_info(TAG,"init done");
	return 0;
}


int influx_header(char *h, size_t l)
{
	if (Header == 0)
		return 0;
	Lock lock(Mtx);
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
		Lock lock(Mtx);
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


int influx_send(const char *data, size_t l)
{
	if (State != running) {
		log_dbug(TAG,"send: not running");
		if (State != connecting)
			influx_init();
		return 0;
	}
	Lock lock(Mtx);
	LOCK_TCPIP_CORE();
	if (TPCB) {
//		log_dbug(TAG,"tcp send '%.*s'",l,data);
		char len[16];
		int n = sprintf(len,"%u\r\n\r\n",l);
		if (err_t e = tcp_write(TPCB,TcpHdr,THL,TCP_WRITE_FLAG_COPY)) {
			log_warn(TAG,"send header failed %d",e);
			State = error;
			return e;
		}
		if (err_t e = tcp_write(TPCB,len,n,TCP_WRITE_FLAG_COPY)) {
			log_warn(TAG,"send length failed %d",e);
			State = error;
			return e;
		}
		if (err_t e = tcp_write(TPCB,data,l,TCP_WRITE_FLAG_COPY)) {
			log_warn(TAG,"send data failed %d",e);
			State = error;
			return e;
		}
		tcp_output(TPCB);
	} else if (UPCB) {
		log_dbug(TAG,"udp send '%.*s'",l,data);
		struct pbuf *pbuf = pbuf_alloc(PBUF_TRANSPORT,l,PBUF_RAM);
		pbuf_take(pbuf,data,l);
		if (err_t e = udp_send(UPCB,pbuf))
			log_warn(TAG,"send failed: %d",e);
		pbuf_free(pbuf);
	}
	UNLOCK_TCPIP_CORE();
	return 0;
}


static void send_element(JsonNumber *e, stream &str, char extra)
{
	if (extra)
		str << extra;
	str << e->name();
	str << '=';
	e->writeValue(str);
}


static char send_elements(JsonObject *o, stream &str, char comma)
{
	const char *name = o->name();
	for (JsonElement *e : o->getChilds()) {
		JsonNumber *n = e->toNumber();
		if (n && n->isValid()) {
			if (comma)
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
	if (RTData == 0)
		return;
	if (State != running) {
		log_dbug(TAG,"rtdata: not connected");
		if (State != connecting)
			influx_init();
		return;
	}
	astream str;
	{
		Lock lock(Mtx);
		str << Header;
	}
	rtd_lock();
	char comma = 0;
	for (JsonElement *e : RTData->getChilds()) {
		if (JsonObject *o = e->toObject()) {
			comma = send_elements(o,str,comma);
		} else if (JsonNumber *n = e->toNumber()) {
			if (n->isValid()) {
				send_element(n,str,comma);
				comma = ',';
			}
		}
	}
	rtd_unlock();
	str << '\n';
	if (comma) {
		log_dbug(TAG,"send rtdata %s",str.c_str());
		influx_send(str.buffer(),str.size());
	} else {
		log_dbug(TAG,"nothing to send");
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
		log_warn(TAG,"out of memory");
	}
	free(LSt);
	LSt = st;
	LNT = nt;
}
#endif


static void send_sys_info(void *)
{
	if (State != running) {
		log_dbug(TAG,"sysinfo: not connected");
		if (State != connecting)
			influx_init();
		return;
	}
	astream str;
	str.write(Header,HL);
	str.printf("uptime=%u,mem32=%u,mem8=%u,memd=%u"
		,uptime()
		,heap_caps_get_free_size(MALLOC_CAP_32BIT)
		,heap_caps_get_free_size(MALLOC_CAP_8BIT)
		,heap_caps_get_free_size(MALLOC_CAP_DMA));
#if configUSE_TRACE_FACILITY == 1
	proc_mon(str);
#endif
	log_dbug(TAG,"sysinfo: %s",str.c_str());
	influx_send(str.buffer(),str.size());
}


int influx_setup()
{
#ifdef SOCKAPI
	Sock = -1;
#endif
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
#ifdef CONFIG_SIGNAL_PROC
	new FuncFact<FnInfluxSend>;
#endif
	action_add("influx!sysinfo",send_sys_info,0,"send system info to influx");
	action_add("influx!rtdata",send_rtdata,0,"send runtime data to influx");
	return 0;
}


int influx(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return arg_invnum(term);
	if (argc == 1) {
		if (Config.has_influx()) {
			const Influx &i = Config.influx();
			if (i.has_hostname())
				term.printf("host       : %s\n",i.hostname().c_str());
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
			term.printf("not configured\n");
		}
	} else if (argc == 2) {
		if (!strcmp("init",args[1])) {
			return influx_init();
		} else if (0 == strcmp(args[1],"clear")) {
			Config.clear_influx();
		} else {
			return arg_invalid(term,args[1]);
		}
	} else if (argc == 3) {
		Influx *i = Config.mutable_influx();
		if (0 == strcmp(args[1],"host")) {
			i->set_hostname(args[2]);
		} else if (0 == strcmp(args[1],"port")) {
			long l = strtol(args[2],0,0);
			if ((l <= 0) || (l > UINT16_MAX)) {
				term.printf("argument out of range\n");
				return 1;
			}
			i->set_port(l);
		} else if (0 == strcmp(args[1],"db")) {
			i->set_database(args[2]);
		} else if (0 == strcmp(args[1],"mm")) {
			i->set_measurement(args[2]);
		} else if (0 == strcmp(args[1],"config")) {
			char *c = strchr(args[2],':');
			if (c == 0) {
				term.printf("':' missing\n");
				return 1;
			}
			char *s = strchr(c+1,'/');
			if (s == 0) {
				term.printf("'/' missing\n");
				return 1;
			}
			long l = strtol(c+1,0,0);
			if ((l <= 0) || (l >= UINT16_MAX)) {
				term.printf("port out of range\n");
				return 1;
			}
			i->set_hostname(args[2],c-args[2]);
			i->set_port(l);
			i->set_measurement(s+1);
		} else if (0 == strcmp(args[1],"send")) {
			influx_send(args[2]);
		} else if (0 == strcmp(args[1],"clear")) {
			if (0 == strcmp(args[2],"db"))
				return Config.mutable_influx()->setByName("database",0);
			if (0 == strcmp(args[2],"mm"))
				return Config.mutable_influx()->setByName("measurement",0);
			return Config.mutable_influx()->setByName(args[2],0);
		} else {
			return arg_invalid(term,args[1]);
		}
	}
	return 0;
}

#endif
