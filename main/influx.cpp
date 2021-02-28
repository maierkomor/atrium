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

#include <lwip/udp.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef write
#undef write
#endif

using namespace std;

static const char TAG[] = "influx";
static int Sock = -1;
static struct sockaddr_in Addr;
static char *Header = 0;
static size_t HL = 0;
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


static int influx_init(void * = 0)
{
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	if (!Config.has_influx() || !Config.has_nodename())
		return 1;
	const Influx &influx = Config.influx();
	if (!influx.has_hostname() || !influx.has_port() || !influx.has_measurement())
		return 1;
	uint32_t ip = resolve_hostname(influx.hostname().c_str());
	if ((IPADDR_NONE == ip) || (ip == 0)) {
		log_error(TAG,"unknown host %s",influx.hostname().c_str());
		return 1;
	}
	Lock lock(Mtx);
	if (Sock != -1) {
		close(Sock);
		Sock = -1;
	}
	if (Header) {
		free(Header);
		Header = 0;
	}
	size_t hl = influx.measurement().size();
	size_t nl = Config.nodename().size() + 1;	// 1 for trailing space of header
	if (nl)
		hl += 6 + nl;
	char *h = (char*)malloc(hl+1);			// 1 for \0
	if (h == 0) {
		log_warn(TAG,"out of memory");
		return 1;
	}
	strcpy(h,influx.measurement().c_str());
	if (nl) {
		strcat(h,",node=");
		strcat(h,Config.nodename().c_str());
	}
	h[hl-1] = ' ';
	h[hl] = 0;
	memset(&Addr,0,sizeof(Addr));
	Addr.sin_family = AF_INET;
	Addr.sin_addr.s_addr = ip;
	Addr.sin_port = htons(influx.port());
	Sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if (Sock == -1) {
		free(h);
		log_error(TAG,"create socket: %s",strerror(errno));
		return 1;
	}
	Header = h;
	HL = hl;
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
	if (Sock == -1) {
		influx_init();
		if (Sock == -1)
			return 1;
		assert(Header);
		assert(HL > 0);
	}
	log_dbug(TAG,"%*s",l,data);
	Lock lock(Mtx);
	if (-1 == sendto(Sock,data,l,0,(const struct sockaddr *) &Addr,sizeof(Addr))) {
		log_error(TAG,"sendto failed: %s",strneterr(Sock));
		close(Sock);
		Sock = -1;
		return 1;
	}
	return 0;
}


static void send_element(JsonElement *e, stream &str, char extra)
{
	if (extra)
		str << extra;
	str << e->name();
	str << '=';
	e->writeValue(str);
}


static char send_elements(JsonObject *o, stream &str, char comma)
{
	JsonElement *e = o->first();
	const char *n = o->name();
	while (e) {
		if (e->toInt()) {
			if (comma)
				str << comma;
			str << n;
			send_element(e,str,'_');
			comma = ',';
		}
		e = e->next();
	}
	return comma;
}


static void send_rtdata(void *)
{
	if (RTData == 0)
		return;
	if ((Header == 0) && influx_init()) {
		log_dbug(TAG,"rtdata: not initialized");
		return;
	}
	astream str;
	{
		Lock lock(Mtx);
		str << Header;
	}
	rtd_lock();
	JsonElement *e = RTData->first();
	char comma = 0;
	while (e) {
		if (JsonObject *o = e->toObject()) {
			comma = send_elements(o,str,comma);
		} else if (e->toInt()||e->toFloat()) {
			send_element(e,str,comma);
			comma = ',';
		}
		e = e->next();
	}
	rtd_unlock();
	log_dbug(TAG,"send rtdata %s",str.c_str());
	influx_send(str.buffer(),str.size());
}


#if configUSE_TRACE_FACILITY == 1
static TaskStatus_t *LSt = 0;
static unsigned LNT = 0;

static void compare_task_sets(TaskStatus_t *st, unsigned nt, stream &s)
{
	for (int i = 0; i < nt; ++i) {
		if (0 == memcmp("IDLE",st[i].pcTaskName,4))
			continue;
		for (int j = 0; j < LNT; ++j) {
			if (st[i].xTaskNumber == LSt[j].xTaskNumber) {
				s << ",task_";
				if (char *c = strchr(st[i].pcTaskName,' '))
					*c = '_';
				s << st[i].pcTaskName;
				s << '=';
				s << st[i].ulRunTimeCounter - LSt[j].ulRunTimeCounter;
				break;
			}
		}
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
	if ((Header == 0) && influx_init()) {
		log_dbug(TAG,"sysinfo: not initialized");
		return;
	}
	astream str;
	str.printf("%suptime=%u,mem32=%u,mem8=%u,memd=%u"
		,Header
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
	Sock = -1;
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
#ifdef CONFIG_SIGNAL_PROC
	new FuncFact<FnInfluxSend>;
#endif
	action_add("influx!sysinfo",send_sys_info,0,"send system info to influx");
	action_add("influx!rtdata",send_rtdata,0,"send runtime data to influx");
	if (Action *a = action_add("influx!start",(void (*)(void*))influx_init,0,"start influx connector"))
		event_callback(StationUpEv,a);
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
				term.printf("port       : %u\n",i.port());
			if (i.has_measurement())
				term.printf("measurement: %s\n",i.measurement().c_str());
			if (Header)
				term.printf("header     : '%s'\n",Header);
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
		} else {
			return arg_invalid(term,args[1]);
		}
	}
	return 0;
}

#endif
