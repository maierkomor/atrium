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


// LIMITATIONS / DESIGN OPTIMIZATIONS:
// - Only packets with "remaining length" < 128 are handled.
//   Other packets are rejected.
// - Only QoS=0: QoS!=0 requires packed IDs and resending
//   reduces RAM/ROM impact
// - no TLS
//   enables direct use of LWIP => reduced RAM/ROM

//#define FEATURE_QOS

#ifdef CONFIG_MQTT
#include "actions.h"
#include "cyclic.h"
#ifdef CONFIG_SIGNAL_PROC
#include "dataflow.h"
#endif
#include "event.h"
#include "func.h"
#include "globals.h"
#include "log.h"
#include "mqtt.h"
#include "mstream.h"
#include "settings.h"
#include "shell.h"
#include "support.h"
#include "swcfg.h"
#include "terminal.h"
#include "ujson.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/tcpip.h>
#include <lwip/tcp.h>

#include <string.h>
#include <time.h>

#include <map>

#define CONNECT 	0x10
#define CONACK		0x20
#define PUBLISH		0x30
#define PUBACK		0x40	// only for QoS > 0
#define SUBSCRIBE	0X82
#define SUBACK		0x90
#define DISCONNECT	0xe0

using namespace std;

int mqtt_setup(void);


#ifdef FEATURE_QOS
struct PubReq
{
	PubReq(char *d, uint16_t l)
	: prev(0)
	, next(0)
	, data(d)
	, len(l)
	, sent(false)
	, alloc(false)
	{
	}

	PubReq(const char *t, const char *v, unsigned l, uint8_t q, bool r)
	: prev(0)
	, next(0)
	{
		memcpy(value,v,l);
	}

	uint16_t topicLen() const
	{
		return data[4] | (data[3] << 8);
	}

	unsigned remainLen() const
	{
		uint8_t d = 
		unsigned len = data[1];
	}

	estring readTopic() const
	{
		return estring(data+5,topicLen());
	}

	bool isTopic(const char *t, size_t tl = 0)
	{
		if (tl == 0)
			tl = strlen(t);
		if (topicLen() != tl)
			return false;
		return memcmp(t,data+5,tl);
	}

	uint8_t paysize() const
	{
		size_t msglen = ((uint8_t *)data+1);
		msglen -=  topicLen();
	}

	uint8_t qos()
	{ return (data[0] >> 1) & 0x3; }

	PubReq *prev,*next;
	char *data;	// only reference for parsing receptions without copy
	uint16_t len;
	bool sent;
	bool alloc;

	private:
	PubReq(const PubReq &);
	PubReq &operator = (const PubReq &);
};
#endif


typedef enum { offline = 0, connecting, running, stopping, error } state_t;


struct MqttClient
{
	MqttClient()
	: pcb(0)
	, mtx(xSemaphoreCreateMutex())
	, sem(xSemaphoreCreateBinary())
	, state(offline)
	{ }

#ifdef FEATURE_QOS
	void add(PubReq *p)
	{
		if (pube) {
			pube->next = p;
			p->prev = pube;
		} else {
			pub0 = p;
		}
		pube = p;
	}

	void remove(PubReq *p)
	{
		if (p->prev)
			p->prev->next = p->next;
		else
			pub0 = p->next;
		if (p->next)
			p->next->prev = p->prev;
		else
			pube = p->prev;
		delete p;
	}
#endif

	struct tcp_pcb *pcb;
	unsigned ltime = 0;
	SemaphoreHandle_t mtx, sem;
#ifdef FEATURE_QOS
	uint16_t pkgid = 0;
	PubReq *pub0 = 0, *pube = 0;
#endif
	multimap<estring,void(*)(const char *,const void*,size_t)> subscriptions;
	map<estring,estring> values;
	map<uint16_t,const char *> subs;
	uint16_t packetid = 0;
	state_t state;
};


static const char TAG[] = "mqtt";
static MqttClient *Client = 0;


static void cpypbuf(void *to, struct pbuf *pbuf, unsigned off, unsigned n)
{
	while (off > pbuf->len) {
		off -= pbuf->len;
		pbuf = pbuf->next;
		assert(pbuf);
	}
	unsigned l;
	do {
		assert(pbuf);
		l = n > (pbuf->len-off) ? (pbuf->len-off) : n;
		memcpy(to,(uint8_t*)pbuf->payload+off,l);
		n -= l;
		pbuf = pbuf->next;
		off = 0;
	} while (l);
}


uint8_t *pbuf_at(struct pbuf *pbuf, unsigned off)
{
//	log_dbug(TAG,"pbuf_at %u/%u",off,pbuf->tot_len);
	while (off > pbuf->len) {
		off -= pbuf->len;
		pbuf = pbuf->next;
		assert(pbuf);
	}
	return ((uint8_t*)pbuf->payload+off);
}


struct pbuf *pbuf_of(struct pbuf *pbuf, unsigned off)
{
//	log_dbug(TAG,"pbuf_of %u/%u",off,pbuf->tot_len);
	while (pbuf && (off >= (pbuf->len))) {
		off -= pbuf->len;
		pbuf = pbuf->next;
	}
	return pbuf;	// returns 0 at end of buffer
}


static void mqtt_term()
{
	if (struct tcp_pcb *pcb = Client->pcb) {
		log_dbug(TAG,"term");
		tcp_abort(pcb);
		Client->pcb = 0;
	}
	Client->state = offline;
}


static void mqtt_restart()
{
	log_dbug(TAG,"restart");
	mqtt_term();
	mqtt_start();
}


static int parse_publish(uint8_t *buf, unsigned rlen, uint8_t qos)
{
	unsigned tl = buf[0] << 8 | buf[1];
	if ((tl + 2) > rlen) {
		log_warn(TAG,"pub invalid length %u",tl);
		return -1;
	}
	estring topic((char*)buf+2,tl);
	auto x = Client->subscriptions.find(topic);
	if (x != Client->subscriptions.end()) {
		uint8_t *pl = buf+2+tl;
		size_t ps = rlen - 2 -tl;
		if (qos) {
			// packet id for qos != 0
			pl += 2;
			ps -= 2;
		}
		log_dbug(TAG,"pub topic %s %u",topic.c_str(),ps);
		x->second(topic.c_str(),pl,ps);
	} else {
		log_warn(TAG,"not subscribed to %s",topic.c_str());
	}
	return 0;
}


static void send_sub(const char *t, bool commit)
{
	size_t tl = strlen(t);
	char request[tl+7];
	char *b = request;
	*b++ = SUBSCRIBE;
	*b++ = sizeof(request) - 2;
	uint16_t pkgid = ++Client->packetid;
	*b++ = pkgid >> 8;
	*b++ = pkgid & 0xff;
	*b++ = 0;
	*b++ = tl;
	memcpy(b,t,tl);
	b += tl;
	*b++ = 0;	// qos = 0
	assert(request+sizeof(request)==b);
	log_dbug(TAG,"subscribe '%s', pkgid=%u",t,pkgid);
	Client->subs.insert(make_pair(pkgid,t));
	err_t e = tcp_write(Client->pcb,request,sizeof(request),TCP_WRITE_FLAG_COPY);
	if (e) {
		log_warn(TAG,"subscribe write %d",e);
	} else if (commit) {
		e = tcp_output(Client->pcb);
		if (e)
			log_warn(TAG,"subscribe output %d",e);
	}
}


static void parse_conack(uint8_t *buf, size_t rlen)
{
	if (rlen != 2) {
		log_warn(TAG,"protocol error, CONACK has invalid length %d",rlen);
	} else {
		uint16_t status = buf[0] << 8 | buf[1];
		if (status == 0) {
			if (Client->pcb == 0) {
				log_warn(TAG,"CONACK without pcb");
				return;
			}
			log_dbug(TAG,"CONACK OK");
			Client->state = running;
			mqtt_pub("version",Version,0,1,1);
			mqtt_pub("reset_reason",ResetReasons[(int)esp_reset_reason()],0,1,1);
			if (!Client->subscriptions.empty()) {
				for (const auto &s : Client->subscriptions)
					send_sub(s.first.c_str(),false);
			}
			if (err_t e = tcp_output(Client->pcb)) {
				log_warn(TAG,"tcp output %d",e);
				Client->state = offline;
				tcp_close(Client->pcb);
				Client->pcb = 0;
			}
			return;
		}
		log_warn(TAG,"CONACK %d",status);
	}
	Client->state = offline;
	tcp_close(Client->pcb);
	Client->pcb = 0;
}


static void parse_suback(uint8_t *buf, size_t rlen)
{
	if (rlen == 3) {
		uint16_t pid = buf[0] << 8 | buf[1];
		log_dbug(TAG,"SUBACK %x",(unsigned)pid);
		if (buf[2] & 0x80) {
			const char *t = "<unknown>";
			auto x = Client->subs.find(pid);
			if (x != Client->subs.end())
				t = x->second;
			log_warn(TAG,"subscription for pid %x, topic %s failed",(unsigned)pid,t);
		}
		if (0 == Client->subs.erase(pid))
			log_warn(TAG,"suback for unknown pid %x",(unsigned)pid);
	} else {
		log_warn(TAG,"suback rlen %u",rlen);
	}

}


static err_t handle_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pbuf, err_t e)
{
	if (e) {
		log_warn(TAG,"recv error %d",e);
		return 0;
	}
	assert(pcb);
	if (0 == pbuf) {
		log_dbug(TAG,"recv: pbuf=0");
		// connection has been closed
		Client->pcb = 0;
		Client->state = offline;
		mqtt_start();
		return 0;
	}
//	log_dbug(TAG,"recv %u/%u: %d",pbuf->len,pbuf->tot_len,e);
	if (pbuf->len == 0)
		return 0;
	unsigned off = 0, tlen = pbuf->tot_len;
	int r = 0;
	do {
		uint8_t type = *pbuf_at(pbuf,off);
		unsigned rlen = *pbuf_at(pbuf,++off);
		if (rlen & 0x80) {
			rlen &= 0x7f;
			uint8_t tmp = *pbuf_at(pbuf,++off);
			if (tmp & 0x80) {
				log_dbug(TAG,"remain length too big");
				pbuf_free(pbuf);
				mqtt_restart();
				return -1;
			}
			rlen |= tmp << 7;
		}
//		log_dbug(TAG,"rlen = %u",rlen);
		bool alloc = false;
		uint8_t *buf = pbuf_at(pbuf,++off);
		if (pbuf_of(pbuf,off) != pbuf_of(pbuf,off+rlen-1)) {
			// packet data spans accross multiple pbuf
			alloc = true;
			buf = (uint8_t *)malloc(rlen);
			cpypbuf(buf,pbuf,off,rlen);
		}
		off += rlen;
		switch (type & 0xf0) {
		case CONACK:
			parse_conack(buf,rlen);
			break;
		case PUBLISH:
			log_dbug(TAG,"PUBLISH");
			r = parse_publish(buf,rlen,(type >> 1) & 0x3);
			break;
		case PUBACK:
			log_dbug(TAG,"PUBACK");
			break;
		case SUBACK:
			parse_suback(buf,rlen);
			break;
		default:
			log_dbug(TAG,"unexpected response %u",buf[0]);
		}
		if (alloc)
			free(buf);
		if (r < 0) {
			pbuf_free(pbuf);
			mqtt_restart();
			return -1;
		}
//		log_dbug(TAG,"advance %u != %u",off,tlen);
	} while (off != tlen);
	tcp_recved(pcb,tlen);
	pbuf_free(pbuf);
	return r;
}


static err_t handle_poll(void *arg, struct tcp_pcb *pcb)
{
	//log_dbug(TAG,"poll %p",pcb);
	return 0;
}


static err_t handle_sent(void *arg, struct tcp_pcb *pcb, u16_t l)
{
//	log_dbug(TAG,"sent %p %u",pcb,l);
	return 0;
}


static void handle_err(void *arg, err_t e)
{
	log_warn(TAG,"handle error %d",e);
	if (e == ERR_ISCONN) {
		Client->state = running;
	} else {
		Client->pcb = 0;
		Client->state = error;
	}
}


static err_t handle_connect(void *arg, struct tcp_pcb *pcb, err_t x)
{
	if (x) {
		log_warn(TAG,"connect error %d",x);
		Client->state = offline;
		tcp_close(pcb);
		Client->pcb = 0;
		return 0;
	}
	log_dbug(TAG,"connect %u",tcp_sndbuf(pcb));
	tcp_recv(pcb,handle_recv);
	tcp_sent(pcb,handle_sent);
	tcp_poll(pcb,handle_poll,2);
	uint8_t flags = 2;	// start with a clean session
	const auto &mqtt = Config.mqtt();
	size_t us = mqtt.username().size();
	if (us)
		flags |= 0x40;
	size_t ps = mqtt.password().size();
	if (ps)
		flags |= 0x80;
	size_t ns = Config.nodename().size();
	char tmp[(us ? us+2 : 0) + (ps ? ps+2 : 0) + ns + 14];
	char *b = tmp;
	*b++ = CONNECT;		// MQTT connect
	*b++ = sizeof(tmp) - 2;	// length of request
	*b++ = 0x0;		// protocol name length high-byte
	*b++ = 0x4;		// protocol name length low-byte
	*b++ = 'M';
	*b++ = 'Q';
	*b++ = 'T';
	*b++ = 'T';
	*b++ = 4;	// protocol version
	*b++ = flags;
	*b++ = 0;	// timeout MSB
	*b++ = 60;	// timeout LSB
	*b++ = (ns>>8)&0xff;	// client-name length MSB
	*b++ = ns & 0xff;	// client-name length LSB
	memcpy(b,Config.nodename().data(),ns);
	b += ns;
	if (us) {	// has username
		*b++ = 0;
		*b++ = us;
		memcpy(b,mqtt.username().data(),us);
		b += us;
	}
	if (ps) {	// has password
		*b++ = 0;	// password length MSB
		*b++ = ps;	// password length LSB
		memcpy(b,mqtt.password().data(),ps);
		//b += ps;
	}
	log_dbug(TAG,"connect write %d",sizeof(tmp));
	err_t e = tcp_write(pcb,tmp,sizeof(tmp),TCP_WRITE_FLAG_COPY);
	if (e) {
		log_warn(TAG,"connect: write error %d",e);
		return e;
	}
	e = tcp_output(pcb);
	if (e)
		log_warn(TAG,"connect: output error %d",e);
	return e;
}


int mqtt_pub(const char *t, const char *v, int len, int retain, int qos)
{
	if (Client == 0)
		return 1;
	if (Client->state != running)
		return 1;
	if ((len == 0) && (v != 0))
		len = strlen(v);
	bool more = (qos & 0x4) != 0;
	qos &= 0x3;
	const char *hn = Config.nodename().c_str();
	size_t hl = Config.nodename().size();
	size_t tl = strlen(t);
	char request[len+hl+tl+5];
	char *b = request;
	qos = 0;	// TODO
	*b++ = PUBLISH | (qos << 1) | retain;
	*b++ = sizeof(request)-2;
	*b++ = 0;
	*b++ = hl+tl+1;
	memcpy(b,hn,hl);
	b += hl;
	*b++ = '/';
	memcpy(b,t,tl);
	b += tl;
	if (qos) {
		// TODO qos: add packet id
	}
	memcpy(b,v,len);
	log_dbug(TAG,"publish %s %u",t,sizeof(request));
	uint8_t flags = TCP_WRITE_FLAG_COPY;
	if (more)
		flags |= TCP_WRITE_FLAG_MORE;
	xSemaphoreTake(Client->mtx,portMAX_DELAY);
	LOCK_TCPIP_CORE();
	err_t e = tcp_write(Client->pcb,request,sizeof(request),flags);
	if (e) {
		log_warn(TAG,"pub write %d",e);
	} else if (!more)  {
		e = tcp_output(Client->pcb);
		if (e)
			log_warn(TAG,"pub output %d",e);
	}
	UNLOCK_TCPIP_CORE();
	xSemaphoreGive(Client->mtx);
	return 0;
}


static void pub_element(JsonElement *e, const char *parent = "")
{
	size_t pl = strlen(parent);
	const char *en = e->name();
	size_t nl = strlen(en);
	char name[pl+nl+2];
	if (pl) {
		memcpy(name,parent,pl);
		name[pl] = '/';
		memcpy(name+pl+1,en,nl+1);
	} else {
		memcpy(name,en,nl+1);
	}
	char buf[128];
	mstream str(buf,sizeof(buf));
	e->writeValue(str);
	if (const char *dim = e->getDimension()) {
		str << ' ';
		str << dim;
	}
	mqtt_pub(name,str.c_str(),str.size(),0,4);
}


static void mqtt_pub_uptime(void * = 0)
{
	if ((Client == 0) || (Client->state != running))
		return;
#ifdef _POSIX_MONOTONIC_CLOCK
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	uint32_t uptime = ts.tv_sec;
#else
	uint64_t uptime = clock() / CLOCKS_PER_SEC;
#endif
	unsigned d = uptime/(60*60*24);
	uptime -= d*(60*60*24);
	unsigned h = uptime/(60*60);
	uptime -= h*60*60;
	unsigned m = uptime/60;
	unsigned t = m + h * 60 + d * 60 * 24;
	int n = 0;
	char value[32];
	if (t != Client->ltime) {
		Client->ltime = t;
		if (d > 0)
			n = snprintf(value,sizeof(value),"%u days, ",d);
		n += snprintf(value+n,sizeof(value)-n,"%d:%02u",h,m);
	}
	if ((n > 0) && (n <= sizeof(value))) {
		if (err_t e = mqtt_pub("uptime",value,n,0,4))
			log_warn(TAG,"publish 'uptime' failed: %d",e);
		else
			log_dbug(TAG,"published uptime %s",value);
	}
}


void mqtt_pub_rtdata(void *)
{
	if ((Client == 0) || (Client->state != running)) {
		if (Config.mqtt().enable() && ((Client == 0) || (Client->state == offline) || (Client->state == error)))
			mqtt_start();
		return;
	}
	mqtt_pub_uptime();
	rtd_lock();
	rtd_update();
	LOCK_TCPIP_CORE();
	for (JsonElement *e : RTData->getChilds()) {
		const char *n = e->name();
		if (!strcmp(n,"node") || !strcmp(n,"version") || !strcmp(n,"reset_reason")) {
			// skip
		} else if (JsonObject *o = e->toObject()) {
			const char *n = e->name();
			for (auto c : o->getChilds()) {
				pub_element(c,n);
			}
		} else {
			pub_element(e);
		}
	}
	rtd_unlock();
	tcp_output(Client->pcb);
	UNLOCK_TCPIP_CORE();
}


int mqtt_sub(const char *t, void (*callback)(const char *,const void *,size_t))
{
	if (Client == 0) {
		log_warn(TAG,"subscribe: not initialized");
		return 1;
	}
	const char *hn = Config.nodename().c_str();
	size_t hl = strlen(hn);
	size_t tl = strlen(t);
	char topic[hl+tl+2];
	memcpy(topic,hn,hl);
	topic[hl] = '/';
	memcpy(topic+hl+1,t,tl+1);
	log_dbug(TAG,"subscribe %s",topic);
	xSemaphoreTake(Client->mtx,portMAX_DELAY);
	LOCK_TCPIP_CORE();
	Client->subscriptions.insert(make_pair((estring)topic,callback));
	if (Client->state == running)
		send_sub(topic,true);
	UNLOCK_TCPIP_CORE();
	xSemaphoreGive(Client->mtx);
	return 0;
}


void mqtt_stop(void *arg)
{
	if ((Client == 0) || (Client->state == offline))
		return;
	log_dbug(TAG,"stop");
	xSemaphoreTake(Client->mtx,portMAX_DELAY);
	char request[] { DISCONNECT, 0 };
	LOCK_TCPIP_CORE();
	err_t e = tcp_write(Client->pcb,request,sizeof(request),TCP_WRITE_FLAG_COPY);
	if (e)
		log_warn(TAG,"write DISCONNECT %d",e);
	else
		tcp_output(Client->pcb);
	e = tcp_close(Client->pcb);
	Client->pcb = 0;
	UNLOCK_TCPIP_CORE();
	if (e)
		log_warn(TAG,"stop: close error %d",e);
	mqtt_term();
	xSemaphoreGive(Client->mtx);
}


void mqtt_start(void *arg)
{
	if (StationMode != station_connected)
		return;
	log_dbug(TAG,"start");
	const auto &mqtt = Config.mqtt();
	if (!mqtt.has_uri() || !mqtt.enable() || !Config.has_nodename()) {
		log_warn(TAG,"incomplete config");
		return;
	}
	if (Client == 0) {
		mqtt_setup();
	} else if ((Client->state != offline) && (Client->state != error)) {
		log_dbug(TAG,"state %d",Client->state);
		return;
	}
	const char *uri = mqtt.uri().c_str();
	if (memcmp(uri,"mqtt://",7)) {
		log_warn(TAG,"invalid uri");
		return;
	}
	char host[strlen(uri)-6];
	strcpy(host,uri+7);
	uint16_t port = 1883;
	char *p = strrchr(host,':');
	if (p) {
		*p++ = 0;
		char *e;
		long l = strtol(p,&e,0);
		if (p != e) {
			if ((l < 0) || (l > UINT16_MAX)) {
				log_warn(TAG,"port out of range");
				return;
			}
			port = l;
		} else {
			log_warn(TAG,"invalid port");
		}
	}
#ifdef CONFIG_IDF_TARGET_ESP32
	ip_addr_t ip;
	ip.type = IPADDR_TYPE_V4;
	ip.u_addr.ip4.addr = resolve_hostname(host);
	if ((ip.u_addr.ip4.addr == IPADDR_NONE) || (ip.u_addr.ip4.addr == 0)) {
#else
	ip4_addr_t ip;
	ip.addr = resolve_hostname(host);
	if ((ip.addr == IPADDR_NONE) || (ip.addr == 0)) {
#endif
		log_warn(TAG,"cannot resolve host: %s",host);
		return;
	}
	Client->subs.clear();
	LOCK_TCPIP_CORE();
	if (Client->pcb)
		tcp_abort(Client->pcb);
	Client->pcb = tcp_new();
	err_t e = tcp_connect(Client->pcb,&ip,port,handle_connect);
	UNLOCK_TCPIP_CORE();
	if (e)
		log_warn(TAG,"tcp_connect: %d",e);
	tcp_err(Client->pcb,handle_err);
}


#ifdef CONFIG_SIGNAL_PROC
class FnMqttSend: public Function
{
	public:
	explicit FnMqttSend(const char *name, bool retain = false)
	: Function(name)
	, m_retain(retain)
	{ }

	void operator () (DataSignal *);
	int setParam(unsigned x, DataSignal *s);

	const char *type() const
	{ return FuncName; }

	static const char FuncName[];

	private:
	bool m_retain;	// currently not usable - no factory
};

const char FnMqttSend::FuncName[] = "mqtt_send";


int FnMqttSend::setParam(unsigned x, DataSignal *s)
{
	s->addFunction(this);
	return 0;
}


void FnMqttSend::operator () (DataSignal *s)
{
	if (s == 0)
		return;
	char buf[64];
	mstream str(buf,sizeof(buf));
	s->toStream(str);
	log_dbug(TAG,"FnMqttSend: %s %s",s->signalName(),str.c_str());
	mqtt_pub(s->signalName(),str.c_str(),str.size(),m_retain,0);
}
#endif


int mqtt_setup(void)
{
	if (!Config.has_mqtt())
		return 0;
	action_add("mqtt!start",mqtt_start,0,"mqtt start");
	action_add("mqtt!stop",mqtt_stop,0,"mqtt stop");
	action_add("mqtt!pub_rtdata",mqtt_pub_rtdata,0,"mqtt publish data");
#ifdef CONFIG_SIGNAL_PROC
	new FuncFact<FnMqttSend>;
#endif
	Client = new MqttClient;
	log_dbug(TAG,"initialized");
	return 0;
}


static void update_signal(const char *t, const void *d, size_t s)
{
	auto i = Client->values.find(t);
	if (i == Client->values.end()) {
		log_warn(TAG,"no value for topic %s",t);
		return;
	}
	i->second.assign((const char *)d,s);
	log_dbug(TAG,"topic %s update to '%.*s'",t,s,d);
}


int mqtt(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return arg_invnum(term);
	MQTT *m = Config.mutable_mqtt();
	if ((argc == 1) || (!strcmp(args[1],"status"))) {
		if (Config.has_mqtt()) {
			term.printf("URI: %s\n"
				"user: %s\n"
				"pass: %s\n"
				"%sabled, %sconnected\n"
				,m->uri().c_str()
				,m->username().c_str()
				,m->password().c_str()
				,m->enable() ? "en" : "dis"
				,Client && Client->state == running ? "" : "not ");
		} else {
			term.printf("not configured\n");
		}
		return 0;
	}
	if (!strcmp(args[1],"uri")) {
		if (argc == 3)
			m->set_uri(args[2]);
		else
			return arg_invalid(term,args[1]);
	} else if (!strcmp(args[1],"user")) {
		if (argc == 3)
			m->set_username(args[2]);
		else if (argc == 2)
			m->clear_username();
		else
			return arg_invalid(term,args[1]);
	} else if (!strcmp(args[1],"pass")) {
		if (argc == 3)
			m->set_password(args[2]);
		else if (argc == 2)
			m->clear_password();
		else
			return arg_invalid(term,args[1]);
	} else if (!strcmp(args[1],"enable")) {
		m->set_enable(true);
		event_callback("wifi`station_up","mqtt!start");
	} else if (!strcmp(args[1],"disable")) {
		m->set_enable(false);
		event_detach("wifi`station_up","mqtt!start");
	} else if (!strcmp(args[1],"clear")) {
		m->clear();
	} else if (!strcmp(args[1],"start")) {
		mqtt_start();
	} else if (!strcmp(args[1],"stop")) {
		mqtt_stop();
	} else if (!strcmp(args[1],"sub")) {
		if (Client == 0) {
			term.println("not initialized");
			return 1;
		}
		if (argc == 2) {
			for (const auto &s : Client->subscriptions)
				term.println(s.first.c_str());
		} else {
			if (Client->subscriptions.find(args[2]) != Client->subscriptions.end()) {
				term.println("already subscribed");
				return 1;
			}
			m->add_subscribtions(args[2]);
			mqtt_sub(args[2],update_signal);
		}
	} else {
		return arg_invalid(term,args[1]);
	}
	return 0;
}


#endif // CONFIG_MQTT
