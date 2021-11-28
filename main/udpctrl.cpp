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

#ifdef CONFIG_UDPCTRL

#include "globals.h"
#include "inetd.h"
#include "log.h"
#include "mem_term.h"
#include "netsvc.h"
#include "shell.h"
#include "support.h"
#include "swcfg.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <lwip/udp.h>

#include <esp_err.h>

#include <string.h>

#ifdef CONFIG_IDF_TARGET_ESP32
#define stacksize 4096
#else
#define stacksize 2048
#endif

struct UdpCtrl
{
	udp_pcb *PCB;
	unsigned Cmds = 0;
	uint16_t Port;
};


struct UdpCmd
{
	struct pbuf *pbuf;
	ip_addr_t ip;
	uint16_t port;

	UdpCmd(struct pbuf *b, ip_addr_t i, uint16_t p)
	: pbuf(b)
	, ip(i)
	, port(p)
	{ }
};


#define TAG MODULE_UDPCTRL
static UdpCtrl *Ctx = 0;


static void udpctrl_session(void *arg)
{
	UdpCmd *c = (UdpCmd *) arg;
	log_dbug(TAG,"%d bytes from %s:%u", c->pbuf->len, inet_ntoa(c->ip), (unsigned)c->port);
	MemTerminal term((const char *)c->pbuf->payload,c->pbuf->len);
	shell(term,false);
	size_t s = term.getSize();
	log_dbug(TAG,"response (%d): '%s'",s,term.getBuffer());
	LWIP_LOCK();
	struct pbuf *r = pbuf_alloc(PBUF_TRANSPORT,s,PBUF_RAM);
	pbuf_take(r,term.getBuffer(),s);
	int e = udp_sendto(Ctx->PCB,r,&c->ip,Ctx->Port);
	pbuf_free(r);
	pbuf_free(c->pbuf);
	LWIP_UNLOCK();
	if (e)
		log_warn(TAG,"send=%d",e);
	delete c;
	vTaskDelete(0);
}


static void recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *ip, u16_t port)
{
	UdpCmd *c = new UdpCmd(p,*ip,port);
	char name[12];
	++Ctx->Cmds;
	sprintf(name,"udpctrl%u",Ctx->Cmds);
	BaseType_t r = xTaskCreatePinnedToCore(udpctrl_session,name,2048,(void*)c,8,NULL,APP_CPU_NUM);
	if (r != pdPASS)  {
		log_warn(TAG,"unable create task %s: %d",name,r);
		pbuf_free(p);
	}
}


int udpc_stats(Terminal &term, int argc, const char *args[])
{
	term.printf("udpctrl: %u packets\n",Ctx->Cmds);
	return 0;
}



int udpctrl_setup(void)
{
	uint16_t p = Config.udp_ctrl_port();
	if (p == 0) {
		log_info(TAG,"disabled");
		return 0;
	}
	Ctx = new UdpCtrl;
	LWIP_LOCK();
	Ctx->PCB = udp_new();
	Ctx->Port = p;
	udp_recv(Ctx->PCB,recv_callback,0);
	udp_bind(Ctx->PCB,IP_ANY_TYPE,p);
	ip_set_option(Ctx->PCB,SO_BROADCAST);
	LWIP_UNLOCK();
	return 0;
}


#endif
