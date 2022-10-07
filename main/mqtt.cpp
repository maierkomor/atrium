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
// - QoS > 0: retries are missing
// - no TLS
//   enables direct use of LWIP => reduced RAM/ROM

#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
#define FEATURE_QOS
#endif

#ifdef CONFIG_MQTT
#include "actions.h"
#ifdef CONFIG_SIGNAL_PROC
#include "dataflow.h"
#endif
#include "event.h"
#include "func.h"
#include "globals.h"
#include "log.h"
#include "mqtt.h"
#include "mstream.h"
#include "netsvc.h"
#include "settings.h"
#include "support.h"
#include "swcfg.h"
#include "tcpio.h"
#include "terminal.h"
#include "env.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <lwip/inet.h>
#include <lwip/tcpip.h>
#include <lwip/priv/tcpip_priv.h>
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
static int mqtt_pub_int(const char *t, const char *v, int len, int retain, int qos, bool needlock);


#define TAG MODULE_MQTT
static struct MqttClient *Client = 0;
static void mqtt_pub_rtdata(void *);
#ifndef CONFIG_IDF_TARGET_ESP8266
static sys_sem_t LwipSem = 0;
#endif


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


#ifdef FEATURE_QOS
struct PubReq
{
	PubReq(const char *t, const char *v, unsigned l)
	: topic(strdup(t))
	{
		value = (char *) malloc(l);
		memcpy(value,v,l);
	}

	PubReq(const PubReq &p)
	: topic(strdup(p.topic))
	, len(p.len)
	{
		value = (char *) malloc(len);
		memcpy(value,p.value,len);
	}

	PubReq(PubReq &&p)
	: topic(p.topic)
	, value(p.value)
	, len(p.len)
	{
		p.topic = 0;
		p.value = 0;
	}

	~PubReq()
	{
		if (topic)
			free(topic);
		if (value)
			free(value);
	}


	char *topic;
	char *value;
	unsigned len;

	private:
	PubReq &operator = (const PubReq &);
};
#endif


struct Subscription
{
	void (*callback)(const char *,const void*,size_t);
	uint16_t lastid = 0;
};


typedef enum { offline = 0, connecting, running, stopping, error } state_t;

static const char *States[] = {
	"offline",
	"connecting",
	"running",
	"stopping",
	"error",
};


struct MqttClient
{
	MqttClient();

	int subscribe(const char *t, void (*callback)(const char *,const void *,size_t));

	struct tcp_pcb *pcb;
	unsigned ltime = 0;
	ip_addr_t ip;
	EnvObject *signals;
	multimap<estring,Subscription> subscriptions;
	map<estring,EnvString *> values;
	map<uint16_t,const char *> subs;
#ifdef FEATURE_QOS
	map<uint16_t,PubReq> pubs;
#endif
	uint16_t packetid = 0;
	SemaphoreHandle_t mtx, sem;
	state_t state;
};


static void mqtt_exe_action(const char *t, const void *d, size_t l)
{
	action_dispatch((const char *)d,l);
}


MqttClient::MqttClient()
: pcb(0)
, mtx(xSemaphoreCreateMutex())
, sem(xSemaphoreCreateBinary())
, state(offline)
{
	char topic[HostnameLen+8];
	memcpy(topic,Hostname,HostnameLen);
	topic[HostnameLen] = '/';
	memcpy(topic+HostnameLen+1,"action",7);
	signals = RTData->add("mqtt");
	subscribe(topic,mqtt_exe_action);
	assert(signals);
#ifdef CONFIG_SIGNAL_PROC
	new FuncFact<FnMqttSend>;
#endif
}


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
	assert(off < pbuf->tot_len);
	while (off >= pbuf->len) {
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


static int mqtt_term()
{
	log_dbug(TAG,"term");
	Client->state = offline;
	if (struct tcp_pcb *pcb = Client->pcb) {
		tcp_abort(pcb);
		Client->pcb = 0;
		return ERR_ABRT;
	}
	return 0;
}


static int parse_publish(uint8_t *buf, unsigned rlen, uint8_t qos)
{
	unsigned tl = buf[0] << 8 | buf[1];
	if ((tl + 2) > rlen) {
		log_warn(TAG,"pub invalid length %u",tl);
		return -1;
	}
	uint8_t *pl = buf+2+tl;
	size_t ps = rlen - 2 - tl;
	char topic[tl+1];
	memcpy(topic,buf+2,tl);
	topic[tl] = 0;
	auto x = Client->subscriptions.find(topic);
	if (x != Client->subscriptions.end()) {
		if (qos) {
			// packet id for qos != 0
			pl += 2;
			ps -= 2;
		}
		log_dbug(TAG,"pub topic %s %u",topic,ps);
		x->second.callback(topic,pl,ps);
	} else {
		log_warn(TAG,"not subscribed to %s",topic);
	}
	return 0;
}


static err_t send_sub(const char *t, bool commit, bool lock)
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
#ifdef CONFIG_IDF_TARGET_ESP8266
	if (lock)
		LWIP_LOCK();
	uint8_t flags = TCP_WRITE_FLAG_COPY;
	if (!commit)
		flags |= TCP_WRITE_FLAG_MORE;
	err_t e = tcp_write(Client->pcb,request,sizeof(request),flags);
	if (e) {
		log_warn(TAG,"subscribe write %d",e);
	} else if (commit) {
		e = tcp_output(Client->pcb);
		if (e)
			log_warn(TAG,"subscribe output %d",e);
	}
	if (lock)
		LWIP_UNLOCK();
#else
	err_t e = 0;
	if (lock) {
		tcpwrite_arg_t r;
		r.pcb = Client->pcb;
		r.data = request;
		r.size = sizeof(request);
		r.name = "subscribe";
		r.sem = LwipSem;
		tcpip_send_msg_wait_sem(commit ? tcpwriteout_fn : tcpwrite_fn,&r,&LwipSem);
	} else {
		uint8_t flags = TCP_WRITE_FLAG_COPY;
		if (!commit)
			flags |= TCP_WRITE_FLAG_MORE;
		e = tcp_write(Client->pcb,request,sizeof(request),flags);
		if (e) {
			log_warn(TAG,"subscribe write %d",e);
		} else if (commit) {
			e = tcp_output(Client->pcb);
			if (e)
				log_warn(TAG,"subscribe output %d",e);
		}
	}
#endif
	return e;
}


static void parse_conack(uint8_t *buf, size_t rlen)
{
	if (rlen != 2) {
		log_warn(TAG,"CONACK: invalid length %d",rlen);
	} else {
		uint16_t status = buf[0] << 8 | buf[1];
		if (status == 0) {
			assert(Client->pcb != 0);
			log_dbug(TAG,"CONACK OK");
			Client->state = running;
			mqtt_pub_int("version",Version,0,1,1,false);
			mqtt_pub_int("reset_reason",ResetReasons[(int)esp_reset_reason()],0,1,1,false);
			for (const auto &s : Client->subscriptions)
				send_sub(s.first.c_str(),false,false);
			if (err_t e = tcp_output(Client->pcb)) {
				log_warn(TAG,"tcp output %d",e);
			} else {
				//log_dbug(TAG,"send ok");
				return;
			}
		} else {
			log_warn(TAG,"CONACK %d",status);
		}
	}
	Client->state = offline;
	tcp_close(Client->pcb);
	Client->pcb = 0;
}


#ifdef FEATURE_QOS
static void parse_puback(uint8_t *buf, size_t rlen)
{
	if (rlen == 2) {
		uint16_t pid = buf[0] << 8 | buf[1];
		auto x = Client->pubs.find(pid);
		if (x != Client->pubs.end()) {
			if ((rlen == 4) && (buf[2] & 0x80)) {
				log_warn(TAG,"PUBACK for pid %x, topic %s failed: reason %x",(unsigned)pid,x->second.topic,buf[2]);
			} else {
				log_dbug(TAG,"PUBACK %x for %s",(unsigned)pid,x->second.topic);
			}
			Client->pubs.erase(x);
		} else {
			log_warn(TAG,"PUBACK %x for unknown topic",(unsigned)pid);
		}
	} else {
		log_warn(TAG,"PUBACK rlen %u",rlen);
	}
}
#endif


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
			log_warn(TAG,"subscribe pid %x, topic %s failed",(unsigned)pid,t);
		}
		if (0 == Client->subs.erase(pid))
			log_warn(TAG,"SUBACK: unknown pid %x",(unsigned)pid);
	} else {
		log_warn(TAG,"SUBACK rlen %u",rlen);
	}

}


static err_t handle_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pbuf, err_t e)
{
	if (e) {
		log_warn(TAG,"recv error %s",strlwiperr(e));
		return 0;
	}
	assert(pcb);
	if (0 == pbuf) {
		log_dbug(TAG,"recv: pbuf=0, err=%d",e);
		if (e == ERR_OK) {
			log_dbug(TAG,"connection closed");
			// connection has been closed
			tcp_close(pcb);
			Client->pcb = 0;
			Client->state = offline;
			//mqtt_start(); - not from here!
			action_dispatch("mqtt!start",0);
		}
		return 0;
	}
	if (pbuf->len == 0)
		return 0;
//	log_hex(TAG, pbuf->len, pbuf->payload, "recv %u: %.*s", pbuf->tot_len);
	unsigned off = 0, tlen = pbuf->tot_len;
	int r = 0;
	do {
		uint8_t type = *pbuf_at(pbuf,off);
		unsigned rlen = *pbuf_at(pbuf,++off);
		if (rlen & 0x80) {
			rlen &= 0x7f;
			uint8_t tmp = *pbuf_at(pbuf,++off);
			if (tmp & 0x80) {
				log_dbug(TAG,"rlen too big");
				pbuf_free(pbuf);
				return mqtt_term();
			}
			rlen |= tmp << 7;
		}
		log_dbug(TAG,"type= %x, rlen = %u",type,rlen);
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
			r = parse_publish(buf,rlen,(type >> 1) & 0x3);
			break;
		case PUBACK:
#ifdef FEATURE_QOS
			parse_puback(buf,rlen);
#endif
			break;
		case SUBACK:
			parse_suback(buf,rlen);
			break;
		default:
			log_dbug(TAG,"unexpected type %u",type>>4);
		}
		if (alloc)
			free(buf);
		if (r < 0) {
			pbuf_free(pbuf);
			return mqtt_term();
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
	log_warn(TAG,"handle error %s",strlwiperr(e));
	if (e == ERR_ISCONN) {
		Client->state = running;
	} else {
		Client->pcb = 0;
		Client->state = error;
	}
}


static err_t handle_connect(void *arg, struct tcp_pcb *pcb, err_t x)
{
	assert(x == ERR_OK);	// according to LWIP docu
	log_info(TAG,"connected %s",inet_ntoa(pcb->remote_ip));
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
	char tmp[(us ? us+2 : 0) + (ps ? ps+2 : 0) + HostnameLen + 14];
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
	*b++ = 0;	// client-name length MSB
	*b++ = HostnameLen;	// client-name length LSB
	memcpy(b,Hostname,HostnameLen);
	b += HostnameLen;
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
		b += ps;
	}
	if (b-tmp != sizeof(tmp))
		log_warn(TAG,"connect fill %d!=%d",b-tmp,sizeof(tmp));
	log_dbug(TAG,"connect write %d",sizeof(tmp));
	err_t e = tcp_write(pcb,tmp,sizeof(tmp),TCP_WRITE_FLAG_COPY);
	if (e) {
		log_warn(TAG,"connect: write error %s",strlwiperr(e));
		return e;
	}

	return mqtt_pub_int("version",Version,strlen(Version),1,0,false);
}


static int mqtt_pub_int(const char *t, const char *v, int len, int retain, int qos, bool needlock)
{
	if ((Client == 0) || (Client->state != running))
		return 1;
	if ((len == 0) && (v != 0))
		len = strlen(v);
	bool more = (qos & 0x4) != 0;
	qos &= 0x3;
	size_t tl = strlen(t);
	char request[len+HostnameLen+tl+5+(qos?2:0)];
	char *b = request;
	*b++ = PUBLISH | (qos << 1) | retain;
	*b++ = sizeof(request)-2;
	*b++ = 0;
	*b++ = HostnameLen+tl+1;
	memcpy(b,Hostname,HostnameLen);
	b += HostnameLen;
	*b++ = '/';
	memcpy(b,t,tl);
	b += tl;
	if (qos) {
		// TODO qos: add packet id
		uint16_t id = ++Client->packetid;
		*b++ = id >> 8;
		*b++ = id & 0xff;
#ifdef FEATURE_QOS
		Client->pubs.insert(make_pair(id,PubReq(t,v,len)));
#endif
	}
	memcpy(b,v,len);
	log_dbug(TAG,"publish %s %.*s",t,len,v);
#ifdef CONFIG_IDF_TARGET_ESP8266
	uint8_t flags = TCP_WRITE_FLAG_COPY;
	if (more)
		flags |= TCP_WRITE_FLAG_MORE;
	if (needlock) {
		LWIP_LOCK();
	}
	const char *m = 0;
	err_t e = tcp_write(Client->pcb,request,sizeof(request),flags);
	if (e) {
		m = "write";
	} else if (!more)  {
		e = tcp_output(Client->pcb);
		m = "output";
	}
	if (needlock) {
		LWIP_UNLOCK();
	}
	if (e)
		log_warn(TAG,"pub %s %d",m,e);
#else
	err_t e = 0;
	if (needlock) {
		tcpwrite_arg_t r;
		r.data = request;
		r.size = sizeof(request);
		r.name = "publish";
		r.sem = LwipSem;
		r.pcb = Client->pcb;
		if (more)
			tcpip_send_msg_wait_sem(tcpwrite_fn,&r,&LwipSem);
		else
			tcpip_send_msg_wait_sem(tcpwriteout_fn,&r,&LwipSem);
	} else {
		uint8_t flags = TCP_WRITE_FLAG_COPY;
		if (more)
			flags |= TCP_WRITE_FLAG_MORE;
		const char *m = 0;
		e = tcp_write(Client->pcb,request,sizeof(request),flags);
		if (e) {
			m = "write";
		} else if (!more) {
			e = tcp_output(Client->pcb);
			m = "output";
		}
		if (e)
			log_warn(TAG,"pub %s %d",m,e);
	}
#endif
	return e;
}


int mqtt_pub(const char *t, const char *v, int len, int retain, int qos)
{
	return mqtt_pub_int(t,v,len,retain,qos,true);
}


int mqtt_pub_nl(const char *t, const char *v, int len, int retain, int qos)
{
	return mqtt_pub_int(t,v,len,retain,qos,false);
}


static void pub_element(EnvElement *e, const char *parent = "")
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
	mqtt_pub_int(name,str.c_str(),str.size(),0,4,true);
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

#ifndef CONFIG_IDF_TARGET_ESP8266
static void mqtt_tcpout_fn(void *)
{
	tcp_output(Client->pcb);
	xSemaphoreGive(LwipSem);
}
#endif


static void mqtt_pub_rtdata(void *)
{
	if ((Client == 0) || (Client->state != running)) {
		if ((StationMode == station_connected) && (Config.mqtt().enable() && ((Client == 0) || (Client->state == offline) || (Client->state == error))))
			mqtt_start();
		return;
	}
	mqtt_pub_uptime();
	rtd_lock();
	for (EnvElement *e : RTData->getChilds()) {
		const char *n = e->name();
		if (!strcmp(n,"node") || !strcmp(n,"version") || !strcmp(n,"reset_reason") || !strcmp(n,"mqtt")) {
			// skip
		} else if (EnvObject *o = e->toObject()) {
			const char *n = e->name();
			for (auto c : o->getChilds()) {
				pub_element(c,n);
			}
		} else {
			pub_element(e);
		}
	}
	rtd_unlock();
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	tcp_output(Client->pcb);
	LWIP_UNLOCK();
#else
	tcpip_send_msg_wait_sem(mqtt_tcpout_fn,0,&LwipSem);
#endif
}


int mqtt_sub(const char *t, void (*callback)(const char *,const void *,size_t))
{
	if (Client == 0)
		Client = new MqttClient;
	size_t tl = strlen(t) + 1;
	char topic[tl+HostnameLen+1];
	memcpy(topic,Hostname,HostnameLen);
	topic[HostnameLen] = '/';
	memcpy(topic+HostnameLen+1,t,tl);
	return Client->subscribe(topic,callback);
}


int MqttClient::subscribe(const char *topic, void (*callback)(const char *,const void *,size_t))
{
	Subscription s;
	s.callback = callback;
	subscriptions.insert(make_pair((estring)topic,s));
	EnvString *str = signals->add(topic,"");
	values.insert(make_pair((estring)topic,str));
	if (state == running) {
		send_sub(topic,true,true);
	}
	xSemaphoreGive(mtx);
	return 0;
}

#ifndef CONFIG_IDF_TARGET_ESP8266
// executed in LwIP context
void mqtt_stop_fn(void *arg)
{
	struct tcp_pcb *pcb = (struct tcp_pcb *) arg;
	uint8_t request[] { DISCONNECT, 0 };
	err_t e = tcp_write(pcb,request,sizeof(request),TCP_WRITE_FLAG_COPY);
	if (e)
		log_warn(TAG,"write DISCONNECT %d",e);
	else
		tcp_output(Client->pcb);
	e = tcp_close(pcb);
	xSemaphoreGive(LwipSem);
}
#endif

void mqtt_stop(void *arg)
{
	if ((Client == 0) || (Client->state == offline))
		return;
	log_dbug(TAG,"stop");
	Lock lock(Client->mtx,__FUNCTION__);
	if (Client->pcb != 0) {
#ifdef CONFIG_IDF_TARGET_ESP8266
		uint8_t request[] { DISCONNECT, 0 };
		LWIP_LOCK();
		err_t e = tcp_write(Client->pcb,request,sizeof(request),TCP_WRITE_FLAG_COPY);
		if (e)
			log_warn(TAG,"write DISCONNECT %d",e);
		else
			tcp_output(Client->pcb);
		e = tcp_close(Client->pcb);
		LWIP_UNLOCK();
		if (e)
			log_warn(TAG,"stop: close error %d",e);
#else
		tcpip_send_msg_wait_sem(mqtt_stop_fn,Client->pcb,&LwipSem);
#endif
		Client->pcb = 0;
	}
	Client->state = offline;
}


static void mqtt_connect(const char *hn, const ip_addr_t *addr, void *arg)
{
	// called as callback from tcpip_task
	log_dbug(TAG,"connect");
	Lock lock(Client->mtx,__FUNCTION__);
	if ((Client->state != connecting) && (Client->state != running)) {
		Client->state = connecting;
		assert(addr);
		uint16_t port = (uint16_t)(unsigned)arg;
		if (Client->pcb) {
			log_warn(TAG,"connect with PCB");
			tcp_abort(Client->pcb);
			Client->pcb = 0;
		} else {
			Client->pcb = tcp_new();
			tcp_err(Client->pcb,handle_err);
			err_t e = tcp_connect(Client->pcb,addr,port,handle_connect);
			if (e)
				log_warn(TAG,"tcp_connect: %d",e);
			else
				log_dbug(TAG,"connect req %s",inet_ntoa(*addr));
		}
	} else {
		log_dbug(TAG,"ignore connect");
	}
}


void mqtt_start(void *arg)
{
	log_dbug(TAG,"start");
	const auto &mqtt = Config.mqtt();
	if (!mqtt.has_uri() || !Config.has_nodename()) {
		log_warn(TAG,"incomplete config");
		return;
	}
	if (StationMode != station_connected) {
		log_dbug(TAG,"station not ready");
		return;
	}
	if (!mqtt.enable()) {
		log_info(TAG,"disabled");
		return;
	}
	if (Client == 0) {
		Client = new MqttClient;
	} else if ((Client->state != offline) && (Client->state != error)) {
		log_dbug(TAG,"state %d",Client->state);
		return;
	}
	const char *uri = mqtt.uri().c_str();
	if (memcmp(uri,"mqtt://",7)) {
		log_warn(TAG,"invalid URI");
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
			return;
		}
	}
	Client->subs.clear();
	log_dbug(TAG,"query %s",host);
	err_t e = query_host(host,&Client->ip,mqtt_connect,(void*)(unsigned)port);
	if (e < 0)
		log_warn(TAG,"query host: %d",e);
	else
		log_dbug(TAG,"started");
}


int mqtt_setup(void)
{
	action_add("mqtt!start",mqtt_start,0,"mqtt start");
	action_add("mqtt!stop",mqtt_stop,0,"mqtt stop");
	action_add("mqtt!pub_rtdata",mqtt_pub_rtdata,0,"mqtt publish data");
	if (0 == event_callback("wifi`station_up","mqtt!start"))
		abort();
	if (Config.has_mqtt() && (Client == 0))
		Client = new MqttClient;
#ifndef CONFIG_IDF_TARGET_ESP8266
	LwipSem = xSemaphoreCreateBinary();
#endif
	log_dbug(TAG,"initialized");
	return 0;
}


static void update_signal(const char *t, const void *d, size_t s)
{
	auto i = Client->values.find(t);
	if (i == Client->values.end()) {
		log_warn(TAG,"topic %s: no value",t);
	} else {
		i->second->set((const char *)d,s);
		log_dbug(TAG,"topic %s update '%.*s'",t,s,d);
	}
}


const char *mqtt(Terminal &term, int argc, const char *args[])
{
	if (argc > 3)
		return "Invalid number of arguments.";
	MQTT *m = Config.mutable_mqtt();
	if ((argc == 1) || (!strcmp(args[1],"status"))) {
		if (Config.has_mqtt()) {
			term.printf("URI: %s\n"
				"user: %s\n"
				"pass: %s\n"
				"%sabled, %s\n"
				, m->uri().c_str()
				, m->username().c_str()
				, m->password().c_str()
				, m->enable() ? "en" : "dis"
				, States[Client ? Client->state : 0]);
		} else {
			return "Not configured.";
		}
		return 0;
	}
	if (!strcmp(args[1],"uri")) {
		if (argc == 3)
			m->set_uri(args[2]);
		else
			return "Invalid argument #1.";
	} else if (!strcmp(args[1],"user")) {
		if (argc == 3)
			m->set_username(args[2]);
		else if (argc == 2)
			m->clear_username();
		else
			return "Invalid argument #1.";
	} else if (!strcmp(args[1],"pass")) {
		if (argc == 3)
			m->set_password(args[2]);
		else if (argc == 2)
			m->clear_password();
		else
			return "Invalid argument #1.";
	} else if (!strcmp(args[1],"enable")) {
		m->set_enable(true);
	} else if (!strcmp(args[1],"disable")) {
		m->set_enable(false);
	} else if (!strcmp(args[1],"clear")) {
		m->clear();
	} else if (!strcmp(args[1],"start")) {
		mqtt_start();
	} else if (!strcmp(args[1],"stop")) {
		m->set_enable(false);
		mqtt_stop();
	} else if (!strcmp(args[1],"sub")) {
		if (Client == 0) {
			return "Not initialized.";
		}
		if (argc == 2) {
			for (const auto &s : Client->subscriptions) 
				term.printf("%s: %s\n",s.first.c_str(),Client->values[s.first]->get());
		} else {
			if (Client->subscriptions.find(args[2]) != Client->subscriptions.end()) {
				return "Already subscribed.";
			}
			m->add_subscribtions(args[2]);
			Client->subscribe(args[2],update_signal);
			return 0;
		}
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


#endif // CONFIG_MQTT
