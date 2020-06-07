/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

#ifdef CONFIG_INFLUX

#include "cyclic.h"
#include "globals.h"
#include "influx.h"
#include "log.h"
#include "support.h"
#include "terminal.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <lwip/udp.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

static const char TAG[] = "influx";
static int Sock = -1;
static struct sockaddr_in Addr;


static void influx_init()
{
	if (Sock != -1) {
		close(Sock);
		Sock = -1;
	}
	if (!Config.has_influx())
		return;
	const Influx &influx = Config.influx();
	if (!influx.has_hostname() || !influx.has_port() || !influx.has_database())
		return;
	uint32_t ip = resolve_hostname(influx.hostname().c_str());
	if (IPADDR_NONE == ip) {
		log_error(TAG,"unable to resolve host %s",influx.hostname().c_str());
		return;
	}
	memset(&Addr,0,sizeof(Addr));
	Addr.sin_family = AF_INET;
	Addr.sin_addr.s_addr = ip;
	Addr.sin_port = htons(influx.port());
	Sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if (Sock == -1)
		log_error(TAG,"unable to create socket: %s",strerror(errno));
	else
		log_info(TAG,"init done");
}


void influx_send(const char *data, size_t l)
{
	if (Sock == -1) {
		influx_init();
		if (Sock == -1)
			return;
	}
	string tmp = Config.influx().database();
	if (Config.has_nodename()) {
		tmp += ",node=";
		tmp += Config.nodename();
	}
	tmp += ' ';
	if (l)
		tmp.append(data,l);
	else
		tmp.append(data);
	tmp += '\n';
	//log_dbug(TAG,"sending '%s'",tmp.c_str());
	if (-1 == sendto(Sock,tmp.data(),tmp.size(),0,(const struct sockaddr *) &Addr,sizeof(Addr)))
		log_error(TAG,"sendto failed: %s",strneterr(Sock));
}


void influx_send(const vector< pair<string,string> > &data)
{
	if (Sock == -1) {
		influx_init();
		if (Sock == -1)
			return;
	}
	string tmp = Config.influx().database();
	if (Config.has_nodename()) {
		tmp += ",node=";
		tmp += Config.nodename();
	}
	tmp += ' ';
	for (const pair<string,string> &i : data) {
		tmp += i.first.c_str();
		tmp += '=';
		tmp += i.second.c_str();
	}
	//log_dbug(TAG,"sending '%s'",tmp.c_str());
	if (-1 == sendto(Sock,tmp.data(),tmp.size(),0,(const struct sockaddr *) &Addr,sizeof(Addr)))
		log_error(TAG,"sendto failed: %s",strneterr(Sock));
}


static unsigned monitor()
{
	unsigned itv = Config.influx().interval();
	if (itv == 0)
		return 60000;
	char buf[128];
	int n;
	n = sprintf(buf,"uptime=%u,mem32=%u,mem8=%u,memd=%u"
		,uptime()
		,heap_caps_get_free_size(MALLOC_CAP_32BIT)
		,heap_caps_get_free_size(MALLOC_CAP_8BIT)
		,heap_caps_get_free_size(MALLOC_CAP_DMA));
#ifdef CONFIG_RELAY
	n += sprintf(buf+n,",relay=%d",RTData.relay() ? 1 : -1);
#endif
	influx_send(buf,n);
	return itv;
}


void influx_setup()
{
	Sock = -1;
	add_cyclic_task("influx",monitor,2000);
}


int influx(Terminal &term, int argc, const char *args[])
{
	if (argc > 3) {
		term.printf("%s: 0-2 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (argc == 1) {
		if (Config.has_influx()) {
			const Influx &i = Config.influx();
			if (i.has_hostname())
				term.printf("host: %s\n",i.hostname().c_str());
			if (i.has_port())
				term.printf("port: %u\n",i.port());
			if (i.has_database())
				term.printf("database: %s\n",i.database().c_str());
			term.printf("interval: %u\n",i.interval());
		} else {
			term.printf("not configured\n");
		}
	} else if (argc == 2) {
		if (!strcmp("-h",args[1])) {
			term.printf(
				"influx config <host>:<port>/<database>\n"
				"influx host <host>\n"
				"influx port <port>\n"
				"influx db <database>\n"
				"influx send <text>\n"
				"influx interval <msec>\n"
				"influx clear\n"
				);
		} else {
			term.printf("unknown argument %s\n",args[1]);
			return 1;
		}
	} else if (argc == 3) {
		if (0 == strcmp(args[1],"host")) {
			Config.mutable_influx()->set_hostname(args[2]);
			influx_init();
		} else if (0 == strcmp(args[1],"port")) {
			long l = strtol(args[2],0,0);
			if ((l <= 0) || (l > UINT16_MAX)) {
				term.printf("argument out of range\n");
				return 1;
			}
			Config.mutable_influx()->set_port(l);
			influx_init();
		} else if (0 == strcmp(args[1],"db")) {
			Config.mutable_influx()->set_database(args[2]);
			influx_init();
		} else if (0 == strcmp(args[1],"interval")) {
			long l = strtol(args[2],0,0);
			if (l < 0) {
				term.printf("interval must be >0\n");
				return 1;
			}
			Config.mutable_influx()->set_interval(l);
		} else if (0 == strcmp(args[1],"config")) {
			char *c = strchr(args[2],':');
			if (c == 0) {
				term.printf("':' missing\n");
				return 1;
			}
			char *s = strchr(c+1,'/');
			if (s == 0) {
				term.printf("'/' missing\n");
				return 1;
			}
			long l = strtol(c+1,0,0);
			if ((l <= 0) || (l >= UINT16_MAX)) {
				term.printf("port out of range\n");
				return 1;
			}
			Influx *i = Config.mutable_influx();
			i->set_hostname(args[2],c-args[2]);
			i->set_port(l);
			i->set_database(s+1);
			influx_init();
		} else if (0 == strcmp(args[1],"clear")) {
			Config.clear_influx();
			influx_init();
		} else if (0 == strcmp(args[1],"send")) {
			influx_send(args[2]);
		} else {
			term.printf("unknown argument %s\n",args[1]);
			return 1;
		}
	}
	return 0;
}

#endif
