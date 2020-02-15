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

#include "globals.h"
#include "inetd.h"
#include "log.h"
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

#ifdef CONFIG_MDNS
#include <mdns.h>	// requires ipv6 on ESP8266
#endif

#ifdef ESP32
#include <soc/soc.h>
#else
#define PRO_CPU_NUM 0
#endif

#define TCP_TIMEOUT 60

#ifdef CONFIG_INETD	// has #else, so this ifdef must be here!!!

#include <vector>

using namespace std;


typedef struct inet_arg
{
	const char *name, *service;
	void (*session)(void*);
	uint16_t port, prio;
	int sock;
	unsigned stack;
} inet_arg_t;


static vector<inet_arg_t> Ports;
static bool Started = false;
static char TAG[] = "inetd";
static fd_set PortFDs;
static int MaxFD;


static int create_socket(int port)
{
	int sock = socket(AF_INET,SOCK_STREAM,0);
	if (sock < 0) {
		log_error(TAG,"error creating server socket: %s",strneterr(sock));
		return -1;
	}
	if (-1 == fcntl(sock,F_SETFL,O_NONBLOCK)) {
		log_error(TAG,"unable to set to non-blocking: %s/%s",strneterr(sock),strerror(errno));
		close(sock);
		return -1;
	}
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		log_error(TAG,"error binding server socket: %s",strneterr(sock));
		close(sock);
		return -1;
	}
	if (listen(sock, 1) < 0) {
		log_error(TAG,"listen failed with %s/%s",strneterr(sock),strerror(errno));
		close(sock);
		return -1;
	}
	log_info(TAG,"listening on port %d",port);
	return sock;
}


static int initialize(fd_set *portfds)
{
	FD_ZERO(portfds);
	wifi_wait();
	int maxfd = 0;
	for (size_t i = 0; i < Ports.size(); ++i) {
		if (Ports[i].sock != -1) {
			close(Ports[i].sock);
			Ports[i].sock = -1;
		}
		int s = create_socket(Ports[i].port);
		if (s == -1)
			continue;
		Ports[i].sock = s;
		FD_SET(s,portfds);
		if (s > maxfd)
			maxfd = s;
#ifdef CONFIG_MDNS
		mdns_service_add(Ports[i].name, Ports[i].service, "_tcp", Ports[i].port, 0, 0);
#endif
	}
	return maxfd;
}


void inet_server(void *ignored)
{
	Started = true;
	MaxFD = initialize(&PortFDs) + 1;
	log_info(TAG,"maximum fd number: %d",MaxFD);
	for (;;) {
		//log_dbug(TAG,"wifi_wait()");
		wifi_wait();

		fd_set rfds = PortFDs;
		//log_dbug(TAG,"select()");
		// timeout is used to pick up changes via inetadm interface
		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		int n = lwip_select(MaxFD,&rfds,0,0,&tv);
		if (n == -1) {
			if (errno != 0) {
				log_warn(TAG,"select returned error: %s",strerror(errno));
				vTaskDelay(200/portTICK_PERIOD_MS);
			}
			continue;
		}
		if (n == 0)
			continue;
		log_info(TAG,"%d new connection",n);
		for (int i = 0; i < Ports.size(); ++i) {
			int sock = Ports[i].sock;
			if (FD_ISSET(sock,&rfds) == 0)
				continue;
			struct sockaddr_in client_addr;
			unsigned socklen = sizeof(client_addr);
			int con = accept(sock, (struct sockaddr *)&client_addr, &socklen);
			if (con < 0) {
				// TODO: what is a better way to deal with error: no more processes
				log_error(TAG,"error accepting connection: %s",strneterr(sock));
				break;
			}
			log_info(TAG,"connection established from %d.%d.%d.%d:%d"
					,client_addr.sin_addr.s_addr & 0xff
					,client_addr.sin_addr.s_addr>>8 & 0xff
					,client_addr.sin_addr.s_addr>>16 & 0xff
					,client_addr.sin_addr.s_addr>>24 & 0xff
					,client_addr.sin_port);
			struct timeval tv;
			tv.tv_sec = TCP_TIMEOUT;
			tv.tv_usec = 0;
			if (0 > setsockopt(con,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)))
				log_warn(TAG,"error setting receive timeout: %s",strneterr(sock));
			char name[configMAX_TASK_NAME_LEN+8];
			snprintf(name,sizeof(name),"%s%02d",Ports[i].name,con);
			name[configMAX_TASK_NAME_LEN] = 0;
			BaseType_t r = xTaskCreatePinnedToCore(Ports[i].session,name,Ports[i].stack,(void*)con,Ports[i].prio,NULL,APP_CPU_NUM);
			if (r != pdPASS) {
				log_error(TAG,"task creation failed: %s",esp_err_to_name(r));
				close(con);
				vTaskDelay(100/portTICK_PERIOD_MS);
			}
		}
	}
}


void listen_tcp(unsigned port, void (*session)(void*), const char *name, const char *service, unsigned prio, unsigned stack)
{
	// inetd version
	if (Started) {
		log_error(TAG,"adding inetd ports after starting inetd has no effect");
	}
	inet_arg_t a;
	a.port = port;
	a.prio = prio;
	a.session = session;
	a.name = name;
	a.service = service;
	a.sock = -1;
	a.stack = stack;
	Ports.push_back(a);
	log_info(TAG,"added %s on port %u",name,port);
}


void inetd_setup(void)
{
	log_info(TAG,"starting inetd");
	BaseType_t r = xTaskCreatePinnedToCore(&inet_server, "inetd", 4096, 0, 2, NULL, PRO_CPU_NUM);
	if (r != pdPASS)
		log_error(TAG,"inetd creation failed: %s",esp_err_to_name(r));
}


int inetadm(Terminal &term, int argc, const char *args[])
{
	if ((argc == 1) || (argc > 3)) {
		term.printf("%s: missing/invalid arguments, use -h for synopsis\n",args[0]);
		return 1;
	}
	if (argc == 2) {
		if (!strcmp(args[1],"-l")) {
			for (size_t i = 0, n = Ports.size(); i < n; ++i)
				term.printf("%s on %u: %s\n",Ports[i].name,Ports[i].port,Ports[i].sock == -1 ? "offline" : "active");
			return 0;
		}
		if (!strcmp(args[1],"-h")) {
			term.printf("inetadm: synopsis\n"
					"list status    : inetadm -l\n"
					"enable service : inetadm -e <service>\n"
					"disable service: inetadm -d <service>\n"
				   );
			return 0;
		}
		term.printf("%s: invalid number of arguments, use -h for synopsis\n",args[0]);
		return 1;
	}
	long p = strtol(args[2],0,0);
	inet_arg_t *serv = 0;
	for (size_t i = 0, n = Ports.size(); i < n; ++i) {
		if ((Ports[i].port == p) || (!strcmp(Ports[i].name,args[2]))) {
			serv = &Ports[i];
			break;
		}
	}
	if (serv == 0) {
		term.printf("unable to find service\n");
		return 1;
	}
	if (!strcmp(args[1],"-e")) {
		if (serv->sock != -1) {
			term.printf("service already enabled\n");
			return 1;
		}
		serv->sock = create_socket(serv->port);
		if (serv->sock == -1) {
			term.printf("error creating socket\n");
			return 1;
		}
		FD_SET(serv->sock,&PortFDs);
		if (serv->sock > MaxFD)
			MaxFD = serv->sock;
#ifdef CONFIG_MDNS
		mdns_service_add(serv->name, serv->service, "_tcp", serv->port, 0, 0);
#endif
		return 0;
	}
	if (!strcmp(args[1],"-d")) {
		term.printf("disable service\n");
		if (serv->sock == -1) {
			term.printf("service already disabled\n");
			return 1;
		}
		FD_CLR(serv->sock,&PortFDs);
		close(serv->sock);
		serv->sock = -1;
		term.printf("service closed\n");
#ifdef CONFIG_MDNS
		mdns_service_remove(serv->service, "_tcp");
#endif
		return 0;
	}
	term.printf("%s: invalid argument, use -h for synopsis\n",args[0]);
	return 1;
}

#else // !CONFIG_INETD

typedef struct tcp_listener_args
{
	void (*session)(void*);
	const char *basename;
	uint16_t port;
	uint16_t stack;
	uint16_t prio;
} tcp_listener_args_t;


void tcp_listener(void *arg)
{
	tcp_listener_args_t *args = (tcp_listener_args_t*)arg;
	const char *basename = args->basename;
	uint16_t port = args->port;
	uint16_t stack = args->stack;
	uint16_t prio = args->prio;
	void (*session)(void*) = args->session;
	free(arg);
	unsigned id = 0;
	int sock = -1;
	for (;;) {
		wifi_wait();
		if (sock < 0) {
			log_info(basename, "starting tcp listener on port %d", port);
			sock = socket(AF_INET, SOCK_STREAM, 0);
			if (sock < 0) {
				log_error(basename,"error creating server socket: %s",strneterr(sock));
				continue;
			}
			struct sockaddr_in server_addr;
			server_addr.sin_family = AF_INET;
			server_addr.sin_port = htons(port);
			server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
				log_error(basename,"error binding server socket: %s",strneterr(sock));
				close(sock);
				sock = -1;
				continue;
			}
			if (listen(sock,1) < 0) {
				log_error(basename,"error listening on server socket: %s",strneterr(sock));
				close(sock);
				sock = -1;
				continue;
			}
		}
		struct sockaddr_in client_addr;
		unsigned socklen = sizeof(client_addr);
		int con = accept(sock, (struct sockaddr *)&client_addr, &socklen);
		if (con < 0) {
			// TODO: what is a better way to deal with error: no more processes
			log_error(basename,"error accepting connection: %s",strneterr(sock));
			close(sock);
			sock = -1;
			continue;
		}
		struct timeval tv;
		tv.tv_sec = TCP_TIMEOUT;
		tv.tv_usec = 0;
		if (0 > setsockopt(con,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)))
			log_warn(basename,"error setting receive timeout: %s",strneterr(sock));
		char name[configMAX_TASK_NAME_LEN+2];
		int n = snprintf(name,sizeof(name),"%s%d",basename,id++);
		assert(n < sizeof(name));
		log_info(basename, "creating sesssion %s, stack %u",name,stack);
		BaseType_t r = xTaskCreatePinnedToCore(session, name, stack, (void*)con, prio, NULL, APP_CPU_NUM);
		if (r != pdPASS) {
			log_error(name,"task creation failed: %s",esp_err_to_name(r));
			close(con);
			vTaskDelay(100/portTICK_PERIOD_MS);
		}
	}
}


void listen_tcp(unsigned port, void (*session)(void*), const char *basename, const char *service, unsigned prio, unsigned stack)
{
	// listener version
	tcp_listener_args_t *args = (tcp_listener_args_t*)malloc(sizeof(tcp_listener_args_t));
	args->port = port;
	args->session = session;
	args->basename = basename;
	args->stack = stack;
	args->prio = prio;
#ifdef CONFIG_MDNS
	mdns_service_add(basename, service, "_tcp", port, 0, 0);
#endif
	BaseType_t r = xTaskCreatePinnedToCore(&tcp_listener, basename, 2048, (void*)args, 5, 0, PRO_CPU_NUM);
	if (r != pdPASS) {
		log_error(basename,"task creation failed: %s",esp_err_to_name(r));
		free(args);
	}
}

#endif // CONFIG_INETD
