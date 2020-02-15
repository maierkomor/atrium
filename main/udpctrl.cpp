/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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
#include "mem_term.h"
#include "shell.h"
#include "support.h"
#include "udpcast.h"
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

static char TAG[] = "udpctrl";
static unsigned Cmds = 0, Exes = 0, Errors = 0;


static void execute_packet(int sock, struct sockaddr_in *a, char *buf, size_t n)
{
	log_info(TAG,"received packed with %d bytes from %s",n,inet_ntoa(*a));
	++Cmds;
	MemTerminal term;
	buf[n] = 0;
	char *eol = streol(buf,n);
	char *at = buf;
	while (eol) {
		*eol = 0;
		++Exes;
		shellexe(term,at);
		at = eol + 1;
		eol = streol(eol+1,n-(at-buf));
	}
	if (-1 == sendto(sock,term.getBuffer(),term.getSize(),0,(struct sockaddr*)a,sizeof(struct sockaddr_in))) {
		log_warn(TAG,"failed to send UDP response: %s",strneterr(sock));
	}
}


static void udpctrl(void *param)
{
	log_info(TAG,"udpctrl starting...");
	for (;;) {
		wifi_wait();
		int sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
		if (sock == -1) {
			++Errors;
			log_error(TAG,"unable to create socket: %s",strneterr(sock));
			vTaskDelay(3000/portTICK_PERIOD_MS);
			continue;
		}
		struct sockaddr_in addr;
		for (;;) {
			char buf[128];
			memset(&addr,0,sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = IPADDR_ANY;
			addr.sin_port = UDP_CTRL_PORT;
			size_t as = sizeof(addr);
			int r = recvfrom(sock,buf,sizeof(buf)-1,0,(struct sockaddr *) &addr,&as);
			if (r > 0)
				execute_packet(sock,&addr,buf,r);
			else if (r < 0)
				break;
		}
		++Errors;
		log_error(TAG,"receive failed: %s",strneterr(sock));
		close(sock);
		vTaskDelay(3000/portTICK_PERIOD_MS);
	}
}



extern "C"
void udpctrl_setup(void)
{
	BaseType_t r = xTaskCreatePinnedToCore(&udpctrl, TAG, 4096, NULL, 5, NULL, PRO_CPU_NUM);
	if (r != pdPASS)
		log_error(TAG,"task creation failed: %s",esp_err_to_name(r));
}


int udpc_stats(Terminal &term, int argc, const char *args[])
{
	term.printf("udpctrl: %u command packets, %u shell executions, %u errors",Cmds,Exes,Errors);
	return 0;
}

#endif
