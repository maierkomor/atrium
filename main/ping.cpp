/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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

#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB

#include "globals.h"
#include "log.h"
#include "netsvc.h"
#include "terminal.h"

#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/netif.h>
#include <lwip/inet_chksum.h>

#define TAG MODULE_NS


typedef struct ping_pkt {
	uint8_t type, code;
	uint16_t chksum, id, seqnr;
	char data[7];
} ping_pkt_t;


typedef struct ping_ctx {
	uint16_t id, seqnr;
	SemaphoreHandle_t sem;
} ping_ctx_t;



static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
	ping_ctx_t *ctx = (ping_ctx_t *)arg;
	uint8_t ihl = *((uint8_t *)p->payload) & 0xf;
	unsigned off = ihl << 2;
	ping_pkt_t *ping = (ping_pkt_t *)((uint8_t*)p->payload+off);
//	log_hex(TAG,ping,sizeof(ping_pkt_t),"ping %p",ping);
	ctx->id = ping->id;
	ctx->seqnr = ping->seqnr;
	pbuf_free(p);
	xSemaphoreGive(ctx->sem);
	return 1;
}


const char *ping(Terminal &t, int argc, const char *args[])
{
	if (argc == 1)
		return "Missing argument.";
	ip_addr_t ip;
	if (resolve_hostname(args[1],&ip))
		return "Hostname lookup failed.";
	char ipstr[64];
	ip2str_r(&ip,ipstr,sizeof(ipstr));
	t.println(ipstr);
	struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
	if (pcb == 0)
		return "Failed to create socket.";
	ping_ctx_t ctx;
	ctx.sem = xSemaphoreCreateBinary();
	ctx.id = 0;
	ctx.seqnr = 0;
	raw_recv(pcb, ping_recv, &ctx);
	raw_bind(pcb, IP_ADDR_ANY);
	struct pbuf *p = pbuf_alloc(PBUF_IP, sizeof(ping_pkt_t), PBUF_RAM);
	ping_pkt_t *ping = (ping_pkt_t*)p->payload;
	ping->chksum = 0;
	ping->id = 0x7536;
	ping->seqnr = htons(1);
	ping->type = ICMP_ECHO;
	ping->code = 0;
	strcpy(ping->data,"Atrium");
	ping->chksum = inet_chksum(ping,sizeof(ping_pkt_t));
	timestamp_t start = timestamp();
	err_t e = raw_sendto(pcb,p,&ip);
	pbuf_free(p);
	if (e != 0) {
		vSemaphoreDelete(ctx.sem);
		return "Send failed.";
	}
	auto x = xSemaphoreTake(ctx.sem,(1000/portTICK_PERIOD_MS));
	vSemaphoreDelete(ctx.sem);
	if (pdTRUE != x)
		return "Timeout.";
	timestamp_t now = timestamp();
	t.printf("response after %luus\n",now-start);
//	log_dbug(TAG,"reply: id=%x, seqnr=%d\n",ctx.id,ctx.seqnr);
	if ((ctx.id != 0x7536) || (ctx.seqnr != htons(1)))
		return "unexpected packet";
	return 0;
}

#endif
