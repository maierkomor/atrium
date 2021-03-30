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

#include "binformats.h"
#include "globals.h"
#include "inetd.h"
#include "mem_term.h"
#include "netsvc.h"
#include "shell.h"
#include "support.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <lwip/udp.h>
#include <sys/socket.h>

#include <esp_system.h>
#include "log.h"
#include <esp_err.h>

#include <string.h>

#ifdef CONFIG_IDF_TARGET_ESP32
#define stacksize 4096
#else
#define stacksize 2048
#endif

static char TAG[] = "udpctrl";
static unsigned Cmds = 0, Exes = 0, Errors = 0;


static void execute_packet(int sock, struct sockaddr_in *a, char *buf, size_t n)
{
	log_dbug(TAG,"packet with %d bytes from %s:%u",n,inet_ntoa(a->sin_addr.s_addr),ntohs(a->sin_port));
	++Cmds;
	MemTerminal term(buf,n);
	shell(term);
	log_dbug(TAG,"response: '%s'",term.getBuffer());
	if (-1 == sendto(sock,term.getBuffer(),term.getSize(),0,(struct sockaddr*)a,sizeof(struct sockaddr_in)))
		log_warn(TAG,"sendto failed: %s",strneterr(sock));
}


#if 0 //ndef CONFIG_INETD
static void udpctrl(void *param)
{
	uint16_t port = htons((uint16_t)(uint32_t)param);
	for (;;) {
		wifi_wait();
		int on = 1;
		struct sockaddr_in addr;
		int ls = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
		int ts = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
		if ((ls == -1) || (ts == -1)) {
			log_error(TAG,"unable to create socket");
			goto restart;
		}
		if (-1 == setsockopt(ls, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)))
			log_warn(TAG,"unable to enable broadcast reception: %s",strneterr(ls));
		bzero(&addr,sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = port;
		if (-1 == bind(ls,(struct sockaddr *)&addr,sizeof(addr)))
			log_error(TAG,"unable to bind socket: %s",strneterr(ls));
		log_info(TAG,"listening on port %u",(uint32_t)param);
		for (;;) {
			char buf[256];
			struct sockaddr_in addr;
			memset(&addr,0,sizeof(addr));
			addr.sin_port = port;
			socklen_t as = sizeof(addr);
			int r = recvfrom(ls,buf,sizeof(buf)-1,0,(struct sockaddr *) &addr,&as);
			addr.sin_port = port;
			if (r < 0)
				break;
			if (r > 0)
				execute_packet(ts,&addr,buf,r);
		}
		log_error(TAG,"receive failed: %s",strneterr(ls));
restart:
		++Errors;
		close(ls);
		close(ts);
		vTaskDelay(3000/portTICK_PERIOD_MS);
	}
}
#endif


int udpc_stats(Terminal &term, int argc, const char *args[])
{
	term.printf("udpctrl: %u commands, %u execs, %u errors\n",Cmds,Exes,Errors);
	return 0;
}


static void proc_packet(void *arg)
{
	char buf[256];
	int ls = (int) arg;
	struct sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	socklen_t as = sizeof(addr);
	int r = recvfrom(ls,buf,sizeof(buf)-1,0,(sockaddr*)&addr,&as);
	if (r > 0) {
		buf[r] = 0;
		log_dbug(TAG,"received '%s' from %d.%d.%d.%d:%d"
				,buf
				,addr.sin_addr.s_addr & 0xff
				,addr.sin_addr.s_addr>>8 & 0xff
				,addr.sin_addr.s_addr>>16 & 0xff
				,addr.sin_addr.s_addr>>24 & 0xff
				,addr.sin_port);
		int ts = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
		addr.sin_port = htons(12719);
		execute_packet(ts,&addr,buf,r);
		close(ts);
	}
	vTaskDelete(0);
}


int udpctrl_setup(void)
{
	uint16_t p = Config.udp_ctrl_port();
	if (p == 0) {
		log_info(TAG,"disabled");
		return 0;
	}
#if 1 //def CONFIG_INETD
	listen_port(p,m_bcast,proc_packet,"udpctrl","udpctrl",8,2048);
#else
	int ls = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if (ls == -1) {
		log_error(TAG,"unable to create socket");
		return 1;
	}
	int on = 1;
	if (-1 == setsockopt(ls, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)))
		log_warn(TAG,"unable to enable broadcast reception: %s",strneterr(ls));
	struct sockaddr_in addr;
	bzero(&addr,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = p;
	if (-1 == bind(ls,(struct sockaddr *)&addr,sizeof(addr))) {
		log_error(TAG,"unable to bind socket: %s",strneterr(ls));
		return 1;
	}
	BaseType_t r = xTaskCreatePinnedToCore(&udpctrl, TAG, stacksize, (void*)(uint32_t)p, 8, NULL, PRO_CPU_NUM);
	if (r != pdPASS) {
		log_error(TAG,"create task: %s",esp_err_to_name(r));
		return 1;
	}
#endif
	return 0;
}


#endif
