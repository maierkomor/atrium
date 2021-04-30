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
	shell(term,false);
	log_dbug(TAG,"response: '%s'",term.getBuffer());
	if (-1 == sendto(sock,term.getBuffer(),term.getSize(),0,(struct sockaddr*)a,sizeof(struct sockaddr_in)))
		log_warn(TAG,"sendto failed: %s",strneterr(sock));
}


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
	listen_port(p,m_bcast,proc_packet,"udpctrl","udpctrl",8,2048);
	return 0;
}


#endif
