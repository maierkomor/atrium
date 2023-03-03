/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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

#ifdef CONFIG_UDNS

#include "actions.h"
#include "cyclic.h"
#include "event.h"
#include "log.h"
#include "netsvc.h"
#include "terminal.h"
#include "udns.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <lwip/err.h>
#include <lwip/igmp.h>
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>
#include <lwip/udp.h>
#include <lwip/mld6.h>
#include <lwip/priv/tcpip_priv.h>

#include <esp_event.h>

#include <string.h>
#include <stdlib.h>


//#define EXTRA_INFO	// buggy! don't use

#if 0
#define log_devel log_local
#else
#define log_devel(...)
#endif
#define DNS_PORT	53
#define MDNS_PORT	5353

#define CLASS_INET	1
#define CLASS_INET6	28
#define TYPE_ADDR	1
#define TYPE_CNAME	5
#define TYPE_PTR	12
#define TYPE_HINFO	13
#define TYPE_TXT	16
#define TYPE_ADDR6	28

#define SIZEOF_ANSWER	10
#define SIZEOF_HEADER	12
#define SIZEOF_IA4	4	// size of ipv4 address
#define SIZEOF_IA6	16	// size of ipv6 address
#define SIZEOF_IP4ANSW	(SIZEOF_ANSWER+SIZEOF_IA4)
#define SIZEOF_IP6ANSW	(SIZEOF_ANSWER+SIZEOF_IA6)

#define MAX_LABELLEN	63

/*************************************************************************

Packet format:
	uint16 transaction-id = 0
	uint16 flags: 0x8000: response, 0x0000 query
	uint16 question count
	uint16 answer count
	uint16 auth-rr count
	uint16 extra-rr count
	question[]
		label[]
		uint16 type	0xc=PTR
		uint16 class	0x1=IN
	answer[]
		label[]
		uint16 type	0x1=A
		uint16 class	0x1=IN
		uint32 ttl
		uint16 length
		char[]
	auth-rr[]	ignored
	extra-rr[]	ignored
	
size: 12 header+Q(label+4)+A(label+10+4)

*************************************************************************/


typedef enum {
	mdns_wifidown,
	mdns_wifiup,
	mdns_pckt0,
	mdns_pckt1,
	mdns_pckt2,
	mdns_up,
	mdns_collission,
} mdns_state_t;


struct flags_t
{
	uint8_t rd:1;
	uint8_t tc:1;
	uint8_t aa:1;
	uint8_t opcode:4;
	uint8_t qr:1;
	uint8_t rcode:4;
	uint8_t z:3;
	uint8_t ra:1;
};


struct Header
{
	uint16_t id;
	flags_t flags;
	uint16_t qcnt;
	uint16_t acnt;
	uint16_t nscnt;
	uint16_t arcnt;
};


struct Answert
{
	uint16_t rr_type, rr_class;
	uint32_t ttl;
	uint16_t rdlen;
};


struct DnsEntry
{
	DnsEntry *next;
	ip_addr_t ip;
	portTickType ttl;
	uint8_t len;
	char host[];
};


struct Query
{
	Query *next;
	portTickType ts;
	uint8_t *buf;
	void *arg;
	void (*cb)(const char *, const ip_addr_t *, void *);
	uint16_t ql;
	bool local;
	uint8_t cnt;	// send count
	char hostname[1];
};


struct CName
{
	struct CName *next;
	char *alias;
	char *cname;

	CName(const char *hn, const char *cn);
	~CName();
};


static void recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *ip, u16_t port);

static void cache_add(const char *hn, size_t hl, ip_addr_t *ip, uint32_t ttl);
static void cache_remove_head();
static ip_addr_t *cache_lookup(const char *hn);
#ifdef EXTRA_INFO
static int add_ptr(const char *);
#endif

static struct udp_pcb *MPCB = 0, *SPCB = 0;
#if LWIP_IPV6
static struct udp_pcb *MPCB6 = 0;
#endif
static DnsEntry *CacheS = 0, *CacheE = 0;
static Query *Queries = 0;;
static CName *CNames = 0;
#ifdef EXTRA_INFO
static char *Ptr = 0;
#endif
static ip4_addr_t IP4;
static ip_addr_t NameServer[4];
static uint16_t CacheSize = 0, MaxCache = 256, Id = 0;
static mdns_state_t State = mdns_wifidown;
static SemaphoreHandle_t Mtx = 0;
#ifndef CONFIG_IDF_TARGET_ESP8266
static SemaphoreHandle_t LwipSem = 0;
#endif


#define TAG MODULE_UDNS

CName::CName(const char *hn, const char *cn)
: next(CNames)
, alias(strdup(hn))
, cname(strdup(cn))
{
	CNames = this;
}


CName::~CName()
{
	free(alias);
	free(cname);
}


#ifdef EXTRA_INFO
static int add_ptr(const char *p)
{
	size_t l = strlen(p);
	size_t pl = Ptr ? strlen(Ptr) : 0;
	char *ptr = (char *) realloc(Ptr,pl+l+2);
	if (ptr == 0)
		return 1;
	ptr[pl] = l;
	memcpy(ptr+pl+1,p,l+1);
	Ptr = ptr;
	return 0;
}
#endif


static void cache_remove_head()
{
	DnsEntry *e = CacheS;
	assert(e);
	log_dbug(TAG,"cache rm %s",e->host);
	CacheSize -= sizeof(DnsEntry);
	CacheSize -= e->len;
	CacheS = e->next;
	free(e);
	if (CacheS == 0)
		CacheE = 0;
}


static int query_remove(const char *hn, ip_addr_t *ip)
{
	int r = 0;
	Query *q = Queries, *prev = 0;
	while (q) {
		Query *n = q->next;
		if (strcmp((char*)q->hostname,hn)) {
			prev = q;
		} else {
			log_devel(TAG,"resolve query %s",q->hostname);
			if (q->cb)
				q->cb(hn,0,q->arg);
			if (prev)
				prev->next = n;
			else
				Queries = n;
			if (q->buf)
				free(q->buf);
			free(q);
			++r;
		}
		q = n;
	}
	return r;
}


static void cache_add(const char *hn, size_t hl, ip_addr_t *ip, uint32_t ttl)
{
//	log_dbug(TAG,"strlen %u, hl %u, %s",strlen(hn),hl,hn);
	DnsEntry *e = CacheS;
	while (e) {
		if (0 == strcmp(e->host,hn)) {
			return;
		}
		e = e->next;
	}
//	log_dbug(TAG,"cache size %u/%u, add %s: %s",CacheSize,MaxCache,hn,ip2str(ip));
	CacheSize += sizeof(DnsEntry)+hl;
	while (CacheSize > MaxCache) {
		assert(CacheS);
		cache_remove_head();
	}
	if (Modules[0] || Modules[TAG]) {
		char ipstr[40];
		if (ip) {
			ip2str_r(ip,ipstr,sizeof(ipstr));
		}
		log_dbug(TAG,"cache add %.*s: %s",hl,hn,ip ? ipstr : "<unknown>");
	}
	e = (DnsEntry *) malloc(sizeof(DnsEntry)+hl);
	memcpy(e->host,hn,hl);
	e->host[hl] = 0;
	e->next = 0;
	if (ip)
		memcpy(&e->ip,ip,sizeof(ip_addr_t));
	else
		bzero(&e->ip,sizeof(e->ip));
	e->len = hl;
	e->ttl = xTaskGetTickCount() + ttl * configTICK_RATE_HZ;
	if (CacheE)
		CacheE->next = e;
	else
		CacheS = e;
	CacheE = e;
}


ip_addr_t *cache_lookup(const char *hn)
{
	Lock lock(Mtx,__FUNCTION__);
	long now = xTaskGetTickCount();
	while (CacheS && (CacheS->ttl < now)) {
		log_dbug(TAG,"entry timed out: %d",CacheS->ttl);
		cache_remove_head();
	}
	DnsEntry *e = CacheS;
	while (e) {
		if (0 == strcmp(e->host,hn)) {
			log_dbug(TAG,"cache hit %s",e->host);
			return &e->ip;
		}
		e = e->next;
	}
	return 0;
}


void add_cname(const char *hostname, const char *cname)
{
	CName *cn = CNames;
	while (cn) {
		if (!strcmp(cn->alias,hostname))
			return;
		cn = cn->next;
	}
	new CName(hostname,cname);
}


static int parseName(struct pbuf *p, size_t &xoff, char *hostname, size_t hoff)
{
	size_t off = xoff;
	log_devel(TAG,"parseName(%u,%u)",off,hoff);
	char *h = hostname + hoff;
	for (;;) {
		int len = pbuf_try_get_at(p,off);
		if (len < 0)
			return -off;
		++off;
		if (len == 0) {
			if ((h > hostname) && (h[-1]== '.'))
				--h;
			*h = 0;
			xoff = off;
//			log_devel(TAG,"parseName1 => %u: '%.*s'",h-hostname,h-hostname,hostname);
			return h-hostname;
		} else if ((len & 0xc0) == 0) {
			if (len != pbuf_copy_partial(p,h,len,off))
				return -off;
			h += len;
			*h++ = '.';
			off += len;
//			log_devel(TAG,"parseName3: %u: '%.*s'",h-hostname,h-hostname,hostname);
		} else if ((len & 0xc0) == 0xc0) {
			int xlen = pbuf_try_get_at(p,off);
			if (xlen < 0)
				return -off;
			++off;
			size_t at = (len ^ 0xc0) << 8 | xlen;
			int x = parseName(p,at,hostname,h-hostname);
			if (x < 0)
			       return x;
//			log_devel(TAG,"parseName4: %d",x);
			h = hostname + x;
			if ((h > hostname) && (h[-1] == '.'))
				--h;
			*h = 0;
			xoff = off;
//			log_devel(TAG,"parseName2 => %u: '%.*s'",h-hostname,h-hostname,hostname);
			return h-hostname;
		} else {
			return -off;
		}
	}

}


static int parseAnswer(struct pbuf *p, size_t off, const ip_addr_t *sender)
{
	char hostname[256];
	int hl = parseName(p,off,hostname,0);
	if (hl <= 0) {
		log_devel(TAG,"parse name %d",hl);
		return hl;
	}
	log_devel(TAG,"off=%u, host %s",off,hostname);
	Answert a;
	if (SIZEOF_ANSWER != pbuf_copy_partial(p,&a,SIZEOF_ANSWER,off))
		return -off;
	a.rr_type = ntohs(a.rr_type);
	a.rr_class = ntohs(a.rr_class);
	a.rdlen = ntohs(a.rdlen);
	off += SIZEOF_ANSWER;
	log_devel(TAG,"answer type %u, class %u, len %u",a.rr_type,a.rr_class,a.rdlen);
	if (a.rr_type == TYPE_CNAME) {
		char cname[256];
		int cnl = parseName(p,off,cname,0);
		log_dbug(TAG,"%s: cname %s",hostname,cname);
		add_cname(hostname,cname);
		Query *q = Queries, *prev = 0;
		while (q) {
			if (0 == strcmp(q->hostname,hostname)) {
				log_devel(TAG,"update query %s => %s",hostname,cname);
				Query *nq = (Query*) realloc(q,sizeof(Query)+cnl+1);
				strcpy(nq->hostname,cname);
				if (prev)
					prev->next = nq;
				else
					Queries = nq;
				prev = nq;
				q = nq->next;
			} else {
				prev = q;
				q = q->next;
			}
		}
		return off;
	}
	if ((a.rr_type != TYPE_ADDR) && (a.rr_type != TYPE_ADDR6)) {
		return off+a.rdlen;
	}
	ip_addr_t ip = IPADDR4_INIT(0);
	char ipstr[64];
	// why mask bit 15?
//	if (((a.rr_class & 0x7fff) == TYPE_ADDR) && (a.rdlen == 4)) {
	if ((a.rr_class == TYPE_ADDR) && (a.rdlen == 4)) {
		if (4 != pbuf_copy_partial(p,ip_2_ip4(&ip),a.rdlen,off)) {
			log_devel(TAG,"copy1 failed");
			return -off;
		}
		log_devel(TAG,"IPv4 %s",ip2str_r(&ip,ipstr,sizeof(ipstr)));
#if defined CONFIG_LWIP_IPV6 || defined ESP32
	// why mask bit 15?
//	} else if (((a.rr_class & 0x7fff) == TYPE_ADDR) && (a.rdlen == 16)) {
	} else if ((a.rr_class == TYPE_ADDR) && (a.rdlen == 16)) {
		if (16 != pbuf_copy_partial(p,ip_2_ip6(&ip),a.rdlen,off)) {
			log_devel(TAG,"copy2 %d@0x%x failed",a.rdlen,off);
			return -off;
		}
		log_devel(TAG,"IPv6 %s",ip2str_r(&ip,ipstr,sizeof(ipstr)));
#endif
	} else if ((a.rr_class & 0x7fff) == TYPE_PTR) {
		log_dbug(TAG,"ignoring PTR");
		return off+a.rdlen;
	} else {
		log_hex(TAG,&a,sizeof(a),"ignoring class 0x%x, len %d",a.rr_class,a.rdlen);
		return off+a.rdlen;
	}
	off += a.rdlen;
	LWIP_UNLOCK();	// is that really ok to avoid the deadlock
	MLock lock(Mtx,__FUNCTION__);
	if (0 == strcmp(hostname,Hostname)) {
		log_warn(TAG,"own hostname on %s",ip2str_r(sender,ipstr,sizeof(ipstr)));
		State = mdns_collission;
	}
	if (query_remove(hostname,&ip))
		cache_add(hostname,hl,&ip,a.ttl);
	lock.unlock();
	log_devel(TAG,"parsing done");
	LWIP_LOCK();
	return off;
}


static inline void sendOwnIp(uint8_t *q, uint16_t ql, uint16_t id, const ip_addr_t *qip, uint16_t port)
{
	log_dbug(TAG,"send own IP");
	// caller ensures: Ctx != 0, State == mdns_up
	size_t bsize = SIZEOF_HEADER+SIZEOF_IP4ANSW+HostnameLen+8;
	uint16_t numans = 1;
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,bsize,PBUF_RAM);
	assert(p->len == p->tot_len);
	Header *h = (Header*) p->payload;
	bzero(h,SIZEOF_HEADER);
	h->id = id;
	h->flags.qr = 1;
	h->flags.aa = 1;
	h->acnt = htons(numans);
	uint8_t *pkt = (uint8_t*)p->payload + SIZEOF_HEADER;
	*pkt++ = HostnameLen;
	memcpy(pkt, Hostname, HostnameLen);
	pkt += HostnameLen;
	memcpy(pkt,"\005local",7);
	pkt += 7;
	Answert a;
	a.rr_type = htons(TYPE_ADDR);
	a.rr_class = htons(CLASS_INET);
	a.ttl = htonl(10000);
	a.rdlen = htons(4);
	memcpy(pkt,&a,SIZEOF_ANSWER);
	pkt += SIZEOF_ANSWER;
	memcpy(pkt,&IP4,SIZEOF_IA4);
	pkt += SIZEOF_IA4;
	assert(pkt-(uint8_t*)p->payload == p->len);
	err_t r;
	struct pbuf *p2 = pbuf_alloc(PBUF_TRANSPORT,bsize,PBUF_RAM);
	assert(p2->len == p2->tot_len);
	pbuf_copy(p2,p);
	r = udp_send(MPCB,p2);
	if (r) {
		char ipstr[64];
		log_warn(TAG,"sendto MPCB %s:%d %s",ip2str_r(qip,ipstr,sizeof(ipstr)),port,strlwiperr(r));
	}
#if defined CONFIG_LWIP_IPV6 || defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	/* causes routing error... why?
	pbuf_copy(p2,p);
	r = udp_send(MPCB6,p2);
	if (r)
		log_warn(TAG,"sendto MPCB6 %s",strlwiperr(r));
	*/
#endif
	r = udp_sendto(SPCB,p,qip,port);
	if (r) {
		char ipstr[64];
		log_warn(TAG,"sendto SPCB %s:%d %s",ip2str_r(qip,ipstr,sizeof(ipstr)),port,strlwiperr(r));
	}
	pbuf_free(p);
	pbuf_free(p2);
}


static inline void sendOwnIp6(uint8_t *q, uint16_t ql, uint16_t id, const ip_addr_t *qip, uint16_t port)
{
#if defined CONFIG_LWIP_IPV6 || defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	// caller ensures: Ctx != 0, State == mdns_up
	if (ip6_addr_isany_val(IP6G))
		return;
//	log_dbug(TAG,"send own IPv6");
	size_t numans = 1;
	size_t bsize = SIZEOF_HEADER+(SIZEOF_IP6ANSW+HostnameLen+8)*numans;
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,bsize,PBUF_RAM);
	assert(p->len == p->tot_len);
	Header *h = (Header*) p->payload;
	bzero(h,SIZEOF_HEADER);
	h->id = id;
	h->flags.qr = 1;
	h->flags.aa = 1;
	h->acnt = htons(numans);
	uint8_t *pkt = (uint8_t*)p->payload + SIZEOF_HEADER;
	*pkt++ = HostnameLen;
	memcpy(pkt, Hostname, HostnameLen);
	pkt += HostnameLen;
	memcpy(pkt,"\005local",7);
	pkt += 7;
	Answert a;
	a.rr_type = htons(TYPE_ADDR6);
	a.rr_class = htons(CLASS_INET);
	a.ttl = htonl(10000);
	a.rdlen = htons(SIZEOF_IA6);
	memcpy(pkt,&a,SIZEOF_ANSWER);
	pkt += SIZEOF_ANSWER;
	log_dbug(TAG,"global ip6 %s",ip6addr_ntoa(&IP6G));
	memcpy(pkt,&IP6G,SIZEOF_IA6);
	pkt += SIZEOF_IA6;
	assert(pkt-(uint8_t*)p->payload == p->len);
	err_t r;
	struct pbuf *p2 = pbuf_alloc(PBUF_TRANSPORT,bsize,PBUF_RAM);
	pbuf_copy(p2,p);
	r = udp_send(MPCB,p2);
	if (r)
		log_warn(TAG,"sendto MPCB %s",strlwiperr(r));
	/* causes routing error... why?
	pbuf_copy(p2,p);
	r = udp_send(MPCB6,p2);
	if (r)
		log_warn(TAG,"sendto MPCB6 %s",strlwiperr(r));
	*/
	pbuf_free(p2);
	r = udp_sendto(SPCB,p,qip,port);
	if (r)
		log_warn(TAG,"sendto SPCB %s",strlwiperr(r));
	pbuf_free(p);
#endif
}


#ifdef EXTRA_INFO
static void sendOwnHostname(uint8_t *q, uint16_t ql, uint16_t id)
{
	log_dbug(TAG,"send own hostname");
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,SIZEOF_HEADER+HostnameLen+DomainnameLen+2,PBUF_RAM);
	assert(p->len == p->tot_len);
	Header *h = (Header*) p->payload;
	bzero(h,SIZEOF_HEADER);
	h->id = id;
	h->flags.qr = 1;
	h->flags.aa = 1;
	h->acnt = htons(1);
	uint8_t *pkt = (uint8_t*)p->payload + SIZEOF_HEADER;
	*pkt++ = HostnameLen;
	memcpy(pkt, Hostname, HostnameLen);
	pkt += HostnameLen;
	*pkt++ = DomainnameLen;
	memcpy(pkt,Domainname,DomainnameLen);
	pkt += DomainnameLen;
	Answert a;
	a.rr_type = htons(TYPE_CNAME);
	a.rr_class = htons(CLASS_INET);
	a.ttl = htonl(10000);
	a.rdlen = htons(4);
	memcpy(pkt,&a,SIZEOF_ANSWER);
	pkt += SIZEOF_ANSWER;
	memcpy(pkt,&IP,4);
	pkt += 4;
	assert(pkt-(uint8_t*)p->payload == p->len);
	udp_send(MPCB,p);
	pbuf_free(p);
}


static void sendHostInfo(uint8_t *q, uint16_t ql, uint16_t id)
{
	log_dbug(TAG,"send own hostinfo");
	const char os[] = "freertos";
#if defined CONFIG_IDF_TARGET_ESP32
	const char cpu[] = "esp32";
#elif defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	const char cpu[] = "esp32-s2";
#elif defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	const char cpu[] = "esp32-s3";
#elif defined CONFIG_IDF_TARGET_ESP32C3
	const char cpu[] = "esp32-c3";
#elif CONFIG_IDF_TARGET_ESP8266
	const char cpu[] = "esp8266";
#else
#error unknown IDF and CPU
#endif
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,SIZEOF_HEADER+sizeof(cpu)+sizeof(os)+2,PBUF_RAM);
	assert(p->len == p->tot_len);
	Header *h = (Header*) p->payload;
	bzero(h,SIZEOF_HEADER);
	h->id = id;
	h->flags.qr = 1;
	h->flags.aa = 1;
	h->acnt = htons(1);
	uint8_t *pkt = (uint8_t*)p->payload + SIZEOF_HEADER;
	*pkt++ = sizeof(cpu)-1;
	memcpy(pkt, cpu, sizeof(cpu)-1);
	pkt += sizeof(cpu)-1;
	*pkt++ = sizeof(os)-1;
	memcpy(pkt, os, sizeof(os)-1);
	pkt += sizeof(os)-1;
	Answert a;
	a.rr_type = htons(TYPE_HINFO);
	a.rr_class = htons(CLASS_INET);
	a.ttl = htonl(10000);
	a.rdlen = htons(4);
	memcpy(pkt,&a,SIZEOF_ANSWER);
	pkt += SIZEOF_ANSWER;
	assert(pkt-(uint8_t*)p->payload == p->len);
	udp_send(MPCB,p);
	pbuf_free(p);
}


static void sendPtrInfo(uint8_t *q, uint16_t ql, uint16_t id)
{
	log_dbug(TAG,"send PTR info");
	size_t l = Ptr ? strlen(Ptr) : 0;
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,SIZEOF_HEADER+l+2,PBUF_RAM);
	assert(p->len == p->tot_len);
	Header *h = (Header*) p->payload;
	bzero(h,SIZEOF_HEADER);
	h->id = id;
	h->flags.qr = 1;
	h->flags.aa = 1;
	h->acnt = htons(1);
	uint8_t *pkt = (uint8_t*)p->payload + SIZEOF_HEADER;
	if (Ptr) {
		memcpy(pkt,Ptr,l+1);
		pkt += l+1;
	} else {
		*pkt = 0;
		++pkt;
	}
	Answert a;
	a.rr_type = htons(TYPE_PTR);
	a.rr_class = htons(CLASS_INET);
	a.ttl = htonl(10000);
	a.rdlen = htons(4);
	memcpy(pkt,&a,SIZEOF_ANSWER);
	pkt += SIZEOF_ANSWER;
	assert(pkt-(uint8_t*)p->payload == p->len);
	udp_send(MPCB,p);
	pbuf_free(p);
}

#endif


static int parseQuestion(struct pbuf *p, size_t qoff, uint16_t id, const ip_addr_t *qip, uint16_t port)
{
	log_devel(TAG,"parse question %d at 0x%x",(int)id,qoff);
	size_t off = qoff;
	char hname[256];
	int x = parseName(p,off,hname,0);
	if (x < 0)
		return x;
	if (off + 4 > p->len)
		return off;
	log_devel(TAG,"qtype at %u",off);
	uint16_t qtype, qclass;
	pbuf_copy_partial(p,&qtype,sizeof(qtype),off);
	off += 2;
	pbuf_copy_partial(p,&qclass,sizeof(qclass),off);
	off += 2;
	qtype = ntohs(qtype);
	qclass = ntohs(qclass);
	log_devel(TAG,"question %d/%d: %s",qtype,qclass,hname);
	if (	((qclass == CLASS_INET) && (0 == strcmp(hname,Hostname)))
		|| ((0 == memcmp(hname,Hostname,HostnameLen)) && (0 == memcmp(hname+HostnameLen,".local",7)))) {
		log_devel(TAG,"question for this host");
		if (qtype == TYPE_ADDR)
			sendOwnIp((uint8_t*)p->payload+qoff,off-qoff,id,qip,port);
		else if (qtype == TYPE_ADDR6)
			sendOwnIp6((uint8_t*)p->payload+qoff,off-qoff,id,qip,port);
#ifdef EXTRA_INFO
		else if (qtype == TYPE_CNAME)
			sendOwnHostname((uint8_t*)p->payload+qoff,off-qoff,id);
		else if (qtype == TYPE_HINFO)
			sendHostInfo((uint8_t*)p->payload+qoff,off-qoff,id);
		else if (qtype == TYPE_PTR)
			sendPtrInfo((uint8_t*)p->payload+qoff,off-qoff,id);
#endif
		else
			log_dbug(TAG,"unsupported qtype %d",qtype);
	}
	return off;
}


static int skipQuestion(struct pbuf *p, size_t off)
{
	log_devel(TAG,"skipQuestion 0x%x",off);
	int len = pbuf_try_get_at(p,off++);
	while (len) {
		if ((len < 0) || (len >= 64))
			return -1;
		off += len;
		len = pbuf_try_get_at(p,off++);
	}
	return off+4;
}


static void recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *ip, u16_t port)
{
//	log_hex(TAG,p->payload,p->len,"packet from %s",ip2str(ip));
	if (Modules[0] || Modules[TAG]) {
		char ipstr[40];
		if (ip)
			ip2str_r(ip,ipstr,sizeof(ipstr));
		log_direct(ll_debug,TAG,"packet from %s",ip ? ipstr : "<unknwon>");
	}
	Header h;
	if (p->len < sizeof(h)) {
		log_devel(TAG,"short packet");
		h.flags.rcode = 1;
	} else {
		pbuf_copy_partial(p,&h,sizeof(h),0);
//		log_hex(TAG,&h,sizeof(h),"header");
		h.qcnt = ntohs(h.qcnt);
		h.acnt = ntohs(h.acnt);
		h.nscnt = ntohs(h.nscnt);
		h.arcnt = ntohs(h.arcnt);
		log_devel(TAG,"qr=%d, rcode=%d",(int)h.flags.qr,(int)h.flags.rcode);
	}
	int e = 0;
	size_t off = sizeof(h);
	if (0 == h.flags.rcode) {
		log_devel(TAG,"opcode=%d, aa=%d, ra=%d, rd=%d, tc=%d, qcnt=%d, acnt=%d, nscnt=%d, arcnt=%d"
				,(int)h.flags.opcode,(int)h.flags.aa,(int)h.flags.ra,(int)h.flags.rd,(int)h.flags.tc
				,(int)h.qcnt,(int)h.acnt,(int)h.nscnt,(int)h.arcnt);
		while (h.qcnt--) {
			int x;
			if ((State == mdns_up) && (0 == h.flags.qr))
				x = parseQuestion(p,off,h.id,ip,port);
			else
				x = skipQuestion(p,off);
			if (x < 0) {
				log_devel(TAG,"skip-question %u: %d",h.qcnt,x);
				pbuf_free(p);
				return;
			}
			off = x;
		}
		unsigned n = h.acnt + h.nscnt + h.arcnt;
//		unsigned n = h.acnt;
		while (n--) {
			int x = parseAnswer(p,off,ip);
			if (x < 0) {
				log_devel(TAG,"parseAnswer(%u) %d",off,x);
				e = -x;
				break;
			}
			off = x;
		}
	} else if (3 == h.flags.rcode) {
		char cname[256];
		int hl = parseName(p,off,cname,0);
		if (hl < 0)
			return;
		log_dbug(TAG,"negative reply for %s",cname);
		query_remove(cname,0);
	}
	if (e) {
		log_hex(TAG,p->payload,p->len,"error %d on packet at %u",e,off);
	}
	pbuf_free(p);
}


static int udns_send_ns(uint8_t *buf,size_t ql)
{
	int c = 0;
	for (int i = 0; i < sizeof(NameServer)/sizeof(NameServer[0]); ++i) {
		if (!ip_addr_isany(&NameServer[i])) {
//			log_hex(TAG,buf,ql,"query %s",ip2str(NameServer[i]));
			struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT,ql,PBUF_RAM);
			err_t e = pbuf_take(pb,buf,ql);
			assert(e == ERR_OK);
			err_t r = udp_sendto(SPCB,pb,&NameServer[i],DNS_PORT);
			pbuf_free(pb);
			if (r == 0)
				++c;
		}
	}
	return c;
}


#ifndef CONFIG_IDF_TARGET_ESP8266
static void query_fn(void *a)
{
	Query *q = (Query *)a;
	if (q->local) {
		if (MPCB) {
			struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT,q->ql,PBUF_RAM);
			err_t e = pbuf_take(pb,q->buf,q->ql);
			assert(e == ERR_OK);
//			log_hex(TAG,pb->payload,pb->len,"sending mDNS query");
			err_t r = udp_send(MPCB,pb);
			q->cnt = (r == 0);
			pbuf_free(pb);		/// TODO/BUG? is this the problem?
		} else {
			log_warn(TAG,"no socket for multi-cast query");
		}
	} else {
		++Id;
		memcpy(q->buf,&Id,2);
		q->cnt = udns_send_ns(q->buf,q->ql);
	}
	if (pdTRUE != xSemaphoreGive(LwipSem))
		abort();
}
#endif


#ifndef CONFIG_IDF_TARGET_ESP8266
typedef struct cb_arg_s {
	void (*cb)(const char *,const ip_addr_t *,void *);
	const char *hn;
	ip_addr_t *ip;
	void *arg;

} cb_arg_t;
void perform_cb_fn(void *arg)
{
	cb_arg_t *a = (cb_arg_t *) arg;
	a->cb(a->hn,a->ip,a->arg);
	if (pdTRUE != xSemaphoreGive(LwipSem))
		abort();
}
#endif


extern "C"
int udns_query(const char *hn, ip_addr_t *ip, void (*cb)(const char *, const ip_addr_t *, void *), void *arg)
{
	if ((hn == 0) || (hn[0] == 0) || (hn[0] == '.'))
		return -1;
	assert(Mtx);
	CName *cn = CNames;
	while (cn) {
		if (!strcmp(hn,cn->alias)) {
			hn = cn->cname;
			break;
		}
		cn = cn->next;
	}
	if (ip_addr_t *ce = cache_lookup(hn)) {
		if (ip)
			*ip = *ce;
		if (cb) {
#ifdef CONFIG_IDF_TARGET_ESP8266
			LWIP_LOCK();
			cb(hn,ce,arg);
			LWIP_UNLOCK();
#else
			cb_arg_t a;
			a.cb = cb;
			a.hn = hn;
			a.ip = ce;
			a.arg = arg;
			tcpip_send_msg_wait_sem(perform_cb_fn,&a,&LwipSem);
#endif
		}
		return 0;
	}
	if (ip)
		*ip = ip_addr_any;
	size_t l = strlen(hn), len = l;
	char labels[l+2], *at = labels;
	memcpy(labels+1,hn,l+1);
	while (char *dot = strchr(at+1,'.')) {
		size_t ll = dot-at-1;
		if (ll > 64)
			return -1;
		l -= ll;
		*at = ll;
		at = dot;
		--l;
	}
	*at = l;
	log_devel(TAG,"labels='%s',len=%u,at-labels=%d,%d",labels,len,at-labels,l);
	Query *q = (Query *) malloc(sizeof(Query)+len+1);
	if (q == 0)
		return -1;
	q->cb = cb;
	q->arg = arg;
	size_t ql = len+2+sizeof(Header)+4;
	q->ql = ql;
	uint8_t *buf = (uint8_t *) malloc(ql);
	if (buf == 0) {
		free(q);
		return -1;
	}
	q->buf = buf;
	q->ts = xTaskGetTickCount();
	bool local = (len > 6) && (0 == memcmp(".local",hn+len-6,6));
	q->local = local;
	memcpy(q->hostname,hn,len+1);
	log_dbug(TAG,"added query for %s",q->hostname);
	bzero(buf,ql);
	buf[2] = 1;	// recursion desired
	buf[5] = 1;	// question count = 1
	memcpy(buf+12,labels,len+2);
	buf[12+2+len+1] = 1;
	buf[12+2+len+3] = 1;
	log_dbug(TAG,"sending %sDNS query",local?"m":"");
	{
		Lock lock(Mtx,__FUNCTION__);
		log_dbug(TAG,"add query %s",q->hostname);
		q->next = Queries;
		Queries = q;
	}
	uint8_t c = 0;
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	if (local) {
		if (MPCB) {
			struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT,ql,PBUF_RAM);
			err_t e = pbuf_take(pb,buf,ql);
			assert(e == ERR_OK);
//			log_hex(TAG,pb->payload,pb->len,"sending mDNS query");
			err_t r = udp_send(MPCB,pb);
			c = (r == 0);
			pbuf_free(pb);
		} else {
			log_warn(TAG,"no socket for multi-cast query");
		}
	} else {
		++Id;
		memcpy(buf,&Id,2);
		c = udns_send_ns(buf,ql);
	}
	LWIP_UNLOCK();
	// free(q->buf); - done when the query is answered and deleted
#else
	tcpip_send_msg_wait_sem(query_fn,q,&LwipSem);
	c = q->cnt;
#endif
	log_dbug(TAG,"%d query sent",c);
	return c;
}


static err_t sendSelfQuery()
{
	// no LWIP_LOCK as cyclic is called from tcpip_task via dns_tmr
	if (HostnameLen == 0) {
		log_dbug(TAG,"cancelled self query");
		return -1;
	}
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,SIZEOF_HEADER+HostnameLen+8+4,PBUF_RAM);
	assert(p->len == p->tot_len);
	bzero(p->payload,SIZEOF_HEADER);
	Header *hdr = (Header *)p->payload;
	hdr->qcnt = htons(1);
	uint8_t *pkt = (uint8_t*)p->payload;
	size_t off = SIZEOF_HEADER;
	pkt[off] = HostnameLen;
	++off;
	memcpy(pkt+off,Hostname,HostnameLen);
	off += HostnameLen;
	memcpy(pkt+off,"\005local",7);
	off += 7;
	pkt[off++] = 1;
	pkt[off++] = 0;
	pkt[off++] = 1;
	pkt[off++] = 0;
	assert(off == SIZEOF_HEADER+HostnameLen+8+4);
	err_t e = udp_send(MPCB,p);
	pbuf_free(p);
//	log_gen(e?ll_warn:ll_debug,TAG,"send self query %.*s.local: %s",HostnameLen,Hostname,strlwiperr(e));
	if (e)
		log_dbug(TAG,"send self query %.*s.local: %s",HostnameLen,Hostname,strlwiperr(e));
	else
		log_dbug(TAG,"send self query %.*s.local",HostnameLen,Hostname);
	return e;
}


#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
static void udns_cyclic_fn(void *arg)
{
	// no LWIP_LOCK as cyclic is called from tcpip_task via dns_tmr
	unsigned *d = (unsigned *)arg;
	assert(Mtx);
	switch (State) {
	case mdns_wifidown:
		if (pdTRUE != xSemaphoreGive(LwipSem))
			abort();
		return;
	case mdns_collission:
	case mdns_up:
		break;
	case mdns_wifiup:
	case mdns_pckt0:
	case mdns_pckt1:
		if (0 == sendSelfQuery()) {
			State = (mdns_state_t)((uint8_t)State + 1);
			*d = 250;
		}
		break;
	case mdns_pckt2:
		log_dbug(TAG,"MDNS up");
		State = mdns_up;
		break;
	default:
		abort();
	}
	if (Queries) {
		Lock lock(Mtx,__FUNCTION__);
		log_dbug(TAG,"locked");
		portTickType ts = xTaskGetTickCount();
		Query *q = Queries;
		while (q) {
			log_dbug(TAG,"%s: q->ts=%u, ts=%u",q->hostname,q->ts,ts);
			if ((q->ts + configTICK_RATE_HZ < ts) && (!q->local)) {
				log_dbug(TAG,"re-query %s",q->hostname);
				udns_send_ns(q->buf,q->ql);
				q->ts = ts;
			}
			q = q->next;
		}
	}
	if (pdTRUE != xSemaphoreGive(LwipSem))
		abort();
}
static unsigned udns_cyclic(void *arg)
{
	unsigned d = 100;
	tcpip_send_msg_wait_sem(udns_cyclic_fn,&d,&LwipSem);
	return d;
}
#elif defined CONFIG_LWIP_IGMP
static unsigned udns_cyclic(void *arg)
{
	// no LWIP_LOCK as cyclic is called from tcpip_task via dns_tmr
	unsigned d = 100;
	if (Mtx == 0)
		return d;
	switch (State) {
	case mdns_wifidown:
		return d;
	case mdns_collission:
	case mdns_up:
		break;
	case mdns_wifiup:
	case mdns_pckt0:
	case mdns_pckt1:
		LWIP_LOCK();
		if (0 == sendSelfQuery()) {
			State = (mdns_state_t)((uint8_t)State + 1);
			d = 250;
		}
		LWIP_UNLOCK();
		break;
	case mdns_pckt2:
		log_dbug(TAG,"MDNS up");
		State = mdns_up;
		break;
	default:
		abort();
	}
	Lock lock(Mtx,__FUNCTION__);
	portTickType ts = xTaskGetTickCount();
	Query *q = Queries;
	while (q) {
		if ((q->ts + configTICK_RATE_HZ < ts) && (!q->local)) {
			log_dbug(TAG,"re-query %s",q->hostname);
			LWIP_LOCK();
			udns_send_ns(q->buf,q->ql);
			LWIP_UNLOCK();
			q->ts = ts;
		}
		q = q->next;
	}
	return d;
}
#endif


static void wifi_down(void *)
{
	assert(Mtx);
	State = mdns_wifidown;
}


static inline void mdns_init_fn(void *)
{
	tcpip_adapter_ip_info_t ipconfig;
	assert(Mtx);
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipconfig)) {
		if (0 != ipconfig.ip.addr)
			IP4 = ipconfig.ip;
	}
#if LWIP_IPV6
	if (MPCB6 == 0) {
		ip_addr_t mdns_ip6;
		int r = ipaddr_aton("ff02::fb",&mdns_ip6);
		assert(r == 1);
		if (err_t e = mld6_joingroup(ip_2_ip6(IP6_ADDR_ANY),ip_2_ip6(&mdns_ip6)))
			log_warn(TAG,"MLD join: %s",strlwiperr(e));
		MPCB6 = udp_new_ip_type(IPADDR_TYPE_V6);
		udp_bind(MPCB6,IP_ANY_TYPE,MDNS_PORT);
		udp_recv(MPCB6,recv_callback,0);
		if (err_t e = udp_connect(MPCB6,&mdns_ip6,MDNS_PORT))
			log_warn(TAG,"connect ff02::fb: %s",strlwiperr(e));
	}
#endif
#if defined CONFIG_LWIP_IGMP || defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	if (MPCB == 0) {
		MPCB = udp_new_ip_type(IPADDR_TYPE_ANY);
		udp_set_multicast_ttl(MPCB,255);
		udp_bind(MPCB,IP_ANY_TYPE,MDNS_PORT);
		udp_recv(MPCB,recv_callback,0);
		ip_addr_t mdns;
		IP_ADDR4(&mdns,224,0,0,251);
		if (err_t e = udp_connect(MPCB,&mdns,MDNS_PORT))
			log_warn(TAG,"connect 224.0.0.251: %s",strlwiperr(e));
		if (err_t e = igmp_joingroup(&ipconfig.ip,ip_2_ip4(&mdns)))
			log_warn(TAG,"unable to join MDNS group: %d",e);
		else
			log_dbug(TAG,"initialized MDNS");
	}
#endif
	State = mdns_wifiup;
#ifndef CONFIG_IDF_TARGET_ESP8266
	assert(LwipSem);
	if (pdTRUE != xSemaphoreGive(LwipSem))
		abort();
#endif
}


static void mdns_init(void *)
{
	log_dbug(TAG,"init");
#ifndef CONFIG_IDF_TARGET_ESP8266
	tcpip_send_msg_wait_sem(mdns_init_fn,0,&LwipSem);
#else
	mdns_init_fn(0);
#endif
}


extern "C"
int udns_add_ptr(const char *ptr)
{
#ifdef EXTRA_INFO
	assert(Mtx);
	return add_ptr(ptr);
#else
	return 1;
#endif
}


extern "C"
void mdns_setup()
{
	log_dbug(TAG,"setup MDNS");
	Action *u = action_add("udns!init_mdns",mdns_init,0,0);
	event_callback(event_id("wifi`station_up"),u);
	Action *d = action_add("udns!wifi_down",wifi_down,0,0);
	event_callback(event_id("wifi`station_down"),d);
}


extern "C"
int udns_set_maxcache(unsigned cs)
{
	assert(Mtx);
	MaxCache = cs;
	while (CacheSize > MaxCache)
		cache_remove_head();
	return 0;
}


extern "C"
int udns_add_nameserver(const char *ns)
{
	assert(Mtx != 0);
	ip_addr_t ip;
	if (inet_aton(ns,&ip) != 1) {
		log_warn(TAG,"set_nameserver(%s): invalid argument",ns);
		return 1;
	}
	int r = 1;
	Lock lock(Mtx,__FUNCTION__);
	for (unsigned x = 0; x < sizeof(NameServer)/sizeof(NameServer[0]); ++x) {
		if (ip_addr_cmp(&NameServer[x],&ip)) {
			log_dbug(TAG,"duplicate nameserver %s",ns);
			r = 2;
			break;
		}
		if (ip_addr_isany(&NameServer[x])) {
			log_dbug(TAG,"nameserver %s",ns);
			NameServer[x] = ip;
			r = 0;
			break;
		}
	}
	if (r == 1)
		log_warn(TAG,"too many nameservers");
	return r;
}


extern "C"
void dns_tmr()
{
//	log_info(TAG,"dns_tmr");
#if defined CONFIG_LWIP_IGMP || defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
//	LWIP_LOCK();
//	udns_cyclic(0);
//	LWIP_UNLOCK();
#endif
}


extern "C"
void dns_init()
{
	//is called after udns_setup();
	log_dbug(TAG,"dns_init");
	if (Mtx == 0) {
		Mtx = xSemaphoreCreateMutex();
		IP4.addr = 0;
		bzero(NameServer,sizeof(NameServer));
		SPCB = udp_new();
		udp_recv(SPCB,recv_callback,0);
		udp_bind(SPCB,IP_ANY_TYPE,5353);
		cyclic_add_task("udns",udns_cyclic,0,250);
#ifndef CONFIG_IDF_TARGET_ESP8266
		LwipSem = xSemaphoreCreateBinary();
#endif
	}
}


extern "C"
void dns_setserver(uint8_t idx, const ip_addr_t *addr)
{
	assert(Mtx);
	if (idx >= sizeof(NameServer)/sizeof(NameServer[0]))
		return;
	if (addr == 0) {
		NameServer[idx] = ip_addr_any;
		return;
	}
	Lock lock(Mtx,__FUNCTION__);
	for (auto &ns : NameServer) {
		if (ip_addr_cmp(&ns,addr))
			return;
		if (ip_addr_isany(&ns)) {
			char ipstr[40];
			log_info(TAG,"add nameserver %s",ip2str_r(addr,ipstr,sizeof(ipstr)));
			ns = *addr;
			return;
		}
	}
}


void udns_update_hostname()
{
	if (Mtx) {
		Lock lock(Mtx,__FUNCTION__);
		State = mdns_wifiup;
	}
}


void udns_setup()
{
}


typedef void (*dns_found_callback)(const char *name, const ip_addr_t *ipaddr, void *callback_arg);

extern "C"
err_t dns_gethostbyname(const char *hostname, ip4_addr_t *addr, dns_found_callback found, void *callback_arg)
{
	ip_addr_t ip;
	ip = ip_addr_any;
	if (udns_query(hostname,&ip,found,callback_arg))
		return ERR_ARG;
	if (ip_addr_isany(&ip))
		return ERR_INPROGRESS;
	if (addr && IP_IS_V4(&ip))
		*addr = *ip_2_ip4(&ip);
	return 0;
}


int udns(Terminal &t, int argc, const char *argv[])
{
	t.print("name server:");
	char ipstr[40];
	for (const auto &ns : NameServer) {
		if (!ip_addr_isany(&ns)) {
			t.printf(" %s",ip2str_r(&ns,ipstr,sizeof(ipstr)));
		}
	}
	t.println("\n\ncache enries:");
	Lock lock(Mtx,__FUNCTION__);
	DnsEntry *e = CacheS;
	while (e) {
//		if (IP_IS_V4(&e->ip))
//			t.printf("%-15s %s\n",ip4addr_ntoa_r(ip_2_ip4(&e->ip),ipstr,sizeof(ipstr)),e->host);

		char ipstr[64];
		t.printf("%-40s %s\n",ip2str_r(&e->ip,ipstr,sizeof(ipstr)),e->host);
		e = e->next;
	}
	/*
#if defined CONFIG_LWIP_IPV6 || defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	e = CacheS;
	while (e) {
		if (IP_IS_V6(&e->ip))
			t.printf("%-40s %s\n",ip6addr_ntoa_r(ip_2_ip6(&e->ip),ipstr,sizeof(ipstr)),e->host);
		e = e->next;
	}
#endif
*/
	CName *cn = CNames;
	while (cn) {
		t.printf("%s => %s\n",cn->alias,cn->cname);
		cn = cn->next;
	}
	return 0;
}
#endif
