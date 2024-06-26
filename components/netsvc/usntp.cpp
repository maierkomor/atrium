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

#include "cyclic.h"
#include "netsvc.h"
#include "log.h"
#include "usntp.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <lwip/err.h>
#include <lwip/igmp.h>
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>
#include <lwip/udp.h>
#include <lwip/priv/tcpip_priv.h>

#include <esp_event.h>
#include <esp_timer.h>

#include <string.h>

#define SNTP_PORT 123
#define SNTP_INERVAL_MS 200
#define DELTA_1970 2208988800UL		// delta from 1900 to 1970
#define DELTA_2036 0x80000000
#define SNTP_PKT_SIZE 48

struct sntp_pckt
{
	uint8_t mode, stratum, poll, precision;
	uint32_t rdelay, rdisper, refid;
	uint32_t refts[2], origts[2], rxts[2], txts[2], keyid, msgdig[4];

};

#define TAG MODULE_SNTP
#ifdef CONFIG_LWIP_IGMP
static struct udp_pcb *MPCB = 0;
#endif
static struct udp_pcb *SPCB = 0, *BPCB = 0;
static uint16_t Interval = 0;
static int64_t LastUpdate = 0;
static char *Server = 0;
static bool Queried = false;
static int64_t KissOfDeath = 0;
#if LWIP_TCPIP_CORE_LOCKING == 0
static sys_sem_t LwipSem = 0;
#endif



static inline void sntp_req_fn(void *arg)
{
	struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT,SNTP_PKT_SIZE,PBUF_RAM);
	assert(pb->len == pb->tot_len);
	bzero(pb->payload,pb->len);
	sntp_pckt *p = (sntp_pckt *) pb->payload;
	p->mode = (4 << 3) | (3 << 0);
	struct timeval tv;
	gettimeofday(&tv,0);
	p->txts[0] = htonl(tv.tv_sec+DELTA_1970);
	p->txts[1] = htonl(tv.tv_usec);
//	log_hex(TAG,pb->payload,SNTP_PKT_SIZE,"send req");
	if (err_t e = udp_send(SPCB,pb))
		log_warn(TAG,"send req %s",strlwiperr(e));
	else
		log_dbug(TAG,"send req");
	pbuf_free(pb);
#if LWIP_TCPIP_CORE_LOCKING == 0
	if (pdTRUE != xSemaphoreGive(LwipSem))
		abort();
#endif
}


static void sntp_connect(const char *hn, const ip_addr_t *addr, void *arg);

static unsigned sntp_cyclic(void *arg)
{
	if (KissOfDeath != 0) {
		if (esp_timer_get_time() > KissOfDeath)
			KissOfDeath = 0;
	} else if (SPCB != 0) {
#if LWIP_TCPIP_CORE_LOCKING == 1
		LWIP_LOCK();
		sntp_req_fn(0);
		LWIP_UNLOCK();
#else
		tcpip_send_msg_wait_sem(sntp_req_fn,0,&LwipSem);
#endif
		if (LastUpdate)
			return (unsigned) Interval * 1000;
	} else if (!Queried && Server && (StationMode == station_connected)) {
		if (err_t e = query_host(Server,0,sntp_connect,0))
			log_warn(TAG,"query %s: %s",Server,esp_err_to_name(e));
		else
			log_info(TAG,"queried %s",Server);
		Queried = true;
	}
	return 1000;
}


static void handle_recv(void *arg, struct udp_pcb *pcb, struct pbuf *pbuf, const ip_addr_t *ip, uint16_t port)
{
	assert(pbuf);
	if (pbuf->len > sizeof(sntp_pckt)) {
		log_hex(TAG,pbuf->payload,pbuf->len,"packet from %s:%d, length %d",inet_ntoa(*ip),(int)port,pbuf->tot_len);
		pbuf_free(pbuf);
		return;
	}
	sntp_pckt p;
	bzero(&p,sizeof(p));
	pbuf_copy_partial(pbuf,&p,pbuf->tot_len,0);
	pbuf_free(pbuf);
	log_dbug(TAG,"packet from %s, mode 0x%x",inet_ntoa(*ip),p.mode);
	// allow clock not synchronized packets (p.mode>>6 == 3)
	if (((p.mode&6) == 4) && ((((p.mode>>6)&3) == 0) || (((p.mode>>6)&3) == 3))) {	// server reply? (4=unicat,5=broadcast)
		if (p.stratum == 0) {
			// kiss-of-death packet
			log_warn(TAG,"received kiss-of-death from %s",inet_ntoa(*ip));
			KissOfDeath = esp_timer_get_time() + 60000000;
		} else {
			struct timeval tv;
			tv.tv_sec = lwip_ntohl(p.txts[0]);
			tv.tv_usec = 0;
			uint32_t fract = lwip_ntohl(p.txts[1]);
			uint32_t usec = 500000;
			do {
				if (fract & 0x80000000)
					tv.tv_usec += usec;
				fract <<= 1;
				usec >>= 1;
			} while (usec > 0);
			if ((tv.tv_sec & 0x80000000) == 0)
				tv.tv_sec += DELTA_2036;
			tv.tv_sec -= DELTA_1970;
			log_dbug(TAG,"set to %lu",tv.tv_sec);
			if (int e = settimeofday(&tv,0)) {
				log_warn(TAG,"set time %d",e);
			} else {
				LastUpdate = esp_timer_get_time();
				if (Modules[0] || Modules[TAG]) {
					char buf[32];
					time_t t = tv.tv_sec;
					ctime_r(&t,buf);
					buf[strlen(buf)-1] = 0;
					log_dbug(TAG,"set to %s",buf);
				}
			}
		}
	} else {
		log_dbug(TAG,"ignore mode %x",p.mode);
	}
}


static void sntp_mc_init_fn(void *)
{
#ifdef CONFIG_LWIP_IGMP
	tcpip_adapter_ip_info_t ipconfig;
	if (ESP_OK == tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipconfig)) {
		if (0 == ipconfig.ip.addr)
			return;
		LWIP_LOCK();
		if (MPCB == 0)
			MPCB = udp_new_ip_type(IPADDR_TYPE_ANY);
		udp_set_multicast_ttl(MPCB,255);
		udp_bind(MPCB,IP_ANY_TYPE,SNTP_PORT);
		udp_recv(MPCB,handle_recv,0);
		ip4_addr_t sntp;
		sntp.addr = PP_HTONL(LWIP_MAKEU32(224,0,1,1));
		if (err_t e = igmp_joingroup(&ipconfig.ip,&sntp))
			log_warn(TAG,"unable to join SNTP group: %d",e);
		else
			log_dbug(TAG,"initialized multi-cast SNTP");
		LWIP_UNLOCK();
	}
#endif
#if LWIP_TCPIP_CORE_LOCKING == 0
	if (pdTRUE != xSemaphoreGive(LwipSem))
		abort();
#endif
}


void sntp_mc_init()
{
#if LWIP_TCPIP_CORE_LOCKING == 1
	sntp_mc_init_fn(0);
#else
	if (LwipSem == 0)
		LwipSem = xSemaphoreCreateBinary();
	tcpip_send_msg_wait_sem(sntp_mc_init_fn,0,&LwipSem);
#endif
}


static void sntp_connect(const char *hn, const ip_addr_t *addr, void *arg)
{
	Queried = false;
	if (addr == 0)
		return;
	udp_pcb *spcb = SPCB;
	if (spcb) {
		SPCB = 0;
		udp_remove(spcb);
	}
	spcb = udp_new();
	udp_recv(spcb,handle_recv,0);
	char ipstr[64];
	ip2str_r(addr,ipstr,sizeof(ipstr));
	if (err_t e = udp_connect(spcb,addr,SNTP_PORT)) {
		log_warn(TAG,"connect %s: %s",ipstr,strlwiperr(e));
		udp_remove(spcb);
	} else {
		log_info(TAG,"connected to %s",ipstr);
		SPCB = spcb;
	}
}


void sntp_set_server(const char *server)
{
	if (Server) {
		char *s = Server;
		Server = 0;
		free(s);
	}
	if (SPCB) {
		LWIP_LOCK();
		udp_remove(SPCB);
		SPCB = 0;
		LWIP_UNLOCK();
	}
	if (server) {
		Server = strdup(server);
		if (Interval == 0) {
			Interval = SNTP_INERVAL_MS;
			cyclic_add_task("sntp",sntp_cyclic,(void*)0,0);
		}
	}
}


static void sntp_bc_init_fn(void *)
{
	if (BPCB)
		udp_remove(BPCB);
	BPCB = udp_new();
	udp_recv(BPCB,handle_recv,0);
	if (err_t e = udp_bind(BPCB,IP_ADDR_ANY,SNTP_PORT)) {
		log_warn(TAG,"bc bind %d",e);
	} else {
		ip_set_option(BPCB,SOF_BROADCAST);
		log_dbug(TAG,"initialized broadcast SNTP");
	}
#if LWIP_TCPIP_CORE_LOCKING == 0
	if (pdTRUE != xSemaphoreGive(LwipSem))
		abort();
#endif
}


void sntp_bc_init()
{
#if LWIP_TCPIP_CORE_LOCKING == 1
	LWIP_LOCK();
	sntp_bc_init_fn(0);
	LWIP_UNLOCK();
#else
	LwipSem = xSemaphoreCreateBinary();
	tcpip_send_msg_wait_sem(sntp_bc_init_fn,0,&LwipSem);
#endif
}



void sntp_set_interval(unsigned itv)
{
	if (Interval == 0)
		cyclic_add_task("sntp",sntp_cyclic,0,0);
	Interval = itv;
}


int64_t sntp_last_update()
{
	return LastUpdate;
}
