/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#ifdef CONFIG_SOCKET_API

#include "globals.h"
#include "inetd.h"
#include "log.h"
#include "lwtcp.h"
#include "netsvc.h"
#include "shell.h"
#include "support.h"
#include "terminal.h"
#include "wifi.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <lwip/tcp.h>
#include <lwip/sockets.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S3
#include <soc/soc.h>
#elif defined CONFIG_IDF_TARGET_ESP32S2
#include <soc/soc.h>
#define APP_CPU_NUM 0
#elif defined CONFIG_IDF_TARGET_ESP32C3
#include <soc/soc.h>
#define APP_CPU_NUM 0
#else
#define PRO_CPU_NUM 0
#endif

#define TCP_TIMEOUT 60


struct InetArg
{
	InetArg *next;
	const char *name;
#ifdef CONFIG_MDNS
	const char *service;
#endif
	void (*session)(void*);
	uint16_t port, stack;
	uint8_t prio;
	inet_mode_t mode;
	int sock;
};


#define TAG MODULE_INETD

static InetArg *Ports = 0;
static bool Started = false;
static fd_set PortFDs;
static int MaxFD = -1;


static int create_udp_socket(int port)
{
	int s = socket(AF_INET,SOCK_DGRAM,port);
	int on = 1;
	if (-1 == setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)))
		log_warn(TAG,"unable to enable broadcast reception: %s",strneterr(s));
	struct sockaddr_in6 addr;
	bzero(&addr,sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(port);
	if (-1 == bind(s,(struct sockaddr *)&addr,sizeof(addr)))
		log_warn(TAG,"unable to bind socket: %s",strneterr(s));
	else
		log_dbug(TAG,"created UDP/%u",port);
	return s;
}


static int create_bcast_socket(int port)
{
	int s = create_udp_socket(port);
	int on = 1;
	if (-1 == setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)))
		log_warn(TAG,"unable to enable broadcast reception: %s",strneterr(s));
	else
		log_dbug(TAG,"set bcast %u, socket %d",port,s);
	return s;
}


static int create_tcp_socket(int port)
{
	log_dbug(TAG,"create TCP %u",port);
	int sock = socket(AF_INET6,SOCK_STREAM,0);
	if (sock < 0) {
		log_error(TAG,"create socket: %s",strneterr(sock));
		return -1;
	}
	if (-1 == fcntl(sock,F_SETFL,O_NONBLOCK)) {
		log_error(TAG,"unable to set to non-blocking: %s/%s",strneterr(sock),strerror(errno));
		close(sock);
		return -1;
	}
	struct sockaddr_in6 server_addr;
	bzero(&server_addr,sizeof(server_addr));
	server_addr.sin6_family = AF_INET6;
	server_addr.sin6_port = htons(port);
	if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		log_error(TAG,"error binding server socket: %s",strneterr(sock));
	} else if (listen(sock, 1) < 0) {
		log_error(TAG,"listen failed with %s/%s",strneterr(sock),strerror(errno));
	} else {
		log_info(TAG,"listening on TCP6/%d",port);
		return sock;
	}
	close(sock);
	return -1;
}


static void init_port(InetArg *p)
{
	if (p->sock != -1) {
		close(p->sock);
		p->sock = -1;
	}
	int s;
	switch (p->mode) {
	case m_sock:
		s = p->sock;
		break;
	case m_tcp:
		s = create_tcp_socket(p->port);
		break;
	case m_udp:
		s = create_udp_socket(p->port);
		break;
	case m_bcast:
		s = create_bcast_socket(p->port);
		break;
	default:
		s = -1;
	}
	if (s != -1) {
		p->sock = s;
		FD_SET(s,&PortFDs);
		if (s > MaxFD)
			MaxFD = s;
#ifdef CONFIG_MDNS
		if (p->port)
			mdns_service_add(p->name, p->service, p->mode == m_tcp ? "_tcp" : "_udp", p->port, 0, 0);
#endif
	}
}

void inet_server(void *ignored)
{
	//log_dbug(TAG,"maximum fd number: %d",MaxFD);
	for (;;) {
		Started = true;

		fd_set rfds = PortFDs;
		int n = lwip_select(MaxFD,&rfds,0,0,0);
		if (n == -1) {
			log_dbug(TAG,"errno %d",errno);
			if (errno != 0) {
				log_warn(TAG,"select failed: %s",strerror(errno));
				vTaskDelay(200/portTICK_PERIOD_MS);
			}
			continue;
		}
		log_dbug(TAG,"%d new connection",n);
		InetArg *p = Ports;
		while (p) {
			if (FD_ISSET(p->sock,&rfds) != 0)
				break;
			p = p->next;
		}
		if (p == 0) {
			log_warn(TAG,"fd not found");
			vTaskDelay(200/portTICK_PERIOD_MS);
			continue;
		}
		int sock = p->sock;
		assert(p->name);
		assert(strlen(p->name) < 16);
		log_dbug(TAG,"service %s",p->name);
		if (p->mode == m_tcp) {
			struct sockaddr_in6 client_addr;
			unsigned socklen = sizeof(client_addr);
			sock = accept(sock, (struct sockaddr *)&client_addr, &socklen);
			if (sock < 0) {
				// TODO: what is a better way to deal with error: no more processes
				log_error(TAG,"error accepting");
				continue;
			}
			log_dbug(TAG,"accepted from %s:%d",inet6_ntoa(client_addr.sin6_addr),client_addr.sin6_port);
			struct timeval tv;
			tv.tv_sec = TCP_TIMEOUT;
			tv.tv_usec = 0;
			if (0 > setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)))
				log_warn(TAG,"set receive timeout: %s",strneterr(sock));
		} else {
			log_warn(TAG,"invalid mode");
		}
		char name[configMAX_TASK_NAME_LEN+8];
		snprintf(name,sizeof(name),"%s%02d",p->name,sock);
		name[configMAX_TASK_NAME_LEN] = 0;
		LwTcp *tcp = new LwTcp(sock);
		BaseType_t r = xTaskCreatePinnedToCore(p->session,name,p->stack,(void*) tcp,p->prio,NULL,APP_CPU_NUM);
		if (r != pdPASS) {
			close(sock);
			log_warn(TAG,"create task: %s",esp_err_to_name(r));
			vTaskDelay(1000/portTICK_PERIOD_MS);
		}
		vTaskDelay(100/portTICK_PERIOD_MS);
	}
}


int listen_port(int port, inet_mode_t mode, void (*session)(LwTcp *), const char *name, const char *service, unsigned prio, unsigned stack)
{
	// inetd version
	if (Started) {
		log_error(TAG,"inetd already started");
		return 1;
	}
	InetArg *a = new InetArg;
	a->prio = prio;
	a->session = (void (*)(void*))session;
	a->name = name;
#ifdef CONFIG_MDNS
	a->service = service;
#endif
	a->stack = stack;
	if (mode == m_sock) {
		a->sock = port;
		a->port = 0;
	} else {
		a->port = port;
		a->sock = -1;
	}
	a->mode = mode;
	a->next = Ports;
	Ports = a;
	log_dbug(TAG,"listen %s on %u",name,port);
	return 0;
}


int inetd_setup(void)
{
	log_dbug(TAG,"init");
	FD_ZERO(&PortFDs);
	InetArg *p = Ports;
	while (p) {
		init_port(p);
		p = p->next;
	}
	++MaxFD;
	BaseType_t r = xTaskCreatePinnedToCore(&inet_server, "inetd", 2560, 0, 5, NULL, PRO_CPU_NUM);
	if (r != pdPASS)
		log_error(TAG,"create inetd: %s",esp_err_to_name(r));
	return 0;
}


const char *inetadm(Terminal &term, int argc, const char *args[])
{
	if (argc == 2) {
		if (!strcmp(args[1],"-l")) {
			InetArg *p = Ports;
			term.printf("%-10s %5s status\n","service","port");
			while (p) {
				term.printf("%-10s %5u %s\n",p->name,p->port,p->sock == -1 ? "off" : "on");
				p = p->next;
			}
			return 0;
		}
		return "Invalid argument #1.";;
	}
	if (argc != 3)
		return "Invalid number of arguments.";
	long l = strtol(args[2],0,0);
	InetArg *p = Ports;
	while (p) {
		if ((p->port == l) || (!strcmp(p->name,args[2])))
			break;
		p = p->next;
	}
	if (p == 0)
		return "Invalid argument #1.";;
	if (!strcmp(args[1],"-e")) {
		if (p->sock != -1) {
			return "Already enabled.";
		}
		init_port(p);
		return 0;
	}
	if (!strcmp(args[1],"-d")) {
		term.printf("disable service\n");
		if (p->sock == -1) {
			return "Already disabled.";
		}
		FD_CLR(p->sock,&PortFDs);
		close(p->sock);
		p->sock = -1;
		term.printf("service closed\n");
#ifdef CONFIG_MDNS
		mdns_service_remove(p->service, p->mode == m_tcp ? "_tcp" : "_udp");
#endif
		return 0;
	}
	return "Invalid argument #1.";;
}

#endif
