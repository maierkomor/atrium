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

#ifdef CONFIG_SYSLOG

#include "binformats.h"
#include "globals.h"
#include "log.h"
#include "netsvc.h"
#include "wifi.h"
#include "versions.h"

#include <string.h>

#include <lwip/udp.h>
#include <sys/socket.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define SYSLOG_PORT 514
#define BUF_SIZE 256

struct syslog_elem
{
	syslog_elem()
	{
	}
	struct timeval tv;
	const char *a;
	char msg[LOG_MAXLEN+1];
	size_t ml;
	log_level_t lvl;
};

static char TAG[] = "rlog";
static SemaphoreHandle_t SyslogLock, SyslogBufSem, SyslogSendSem;
static syslog_elem SyslogQ[8];
static uint8_t SyslogIn = 0, SyslogOut = 0;
static bool SyslogRunning = false;
static int LastErr = 0;
static unsigned Skipped = 0;


extern "C"
void log_syslog(log_level_t lvl, const char *a, const char *msg, size_t ml)
{
	if (!SyslogRunning)
		return;
	// header: pri version timestamp hostname app-name procid msgid
	struct timeval tv;
	tv.tv_sec = 0;
	gettimeofday(&tv,0);
	if (pdFALSE == xSemaphoreTake(SyslogBufSem,50/portTICK_PERIOD_MS)) {
//		con_print("syslog skip");
		++Skipped;
	} else {
		xSemaphoreTake(SyslogLock,portMAX_DELAY);
		syslog_elem *e = SyslogQ+SyslogIn;
		++SyslogIn;
		if (SyslogIn == sizeof(SyslogQ)/sizeof(SyslogQ[0]))
			SyslogIn = 0;
		e->lvl = lvl;
		e->tv = tv;
		e->a = a;
		assert(ml<=LOG_MAXLEN);
		e->ml = ml;
		memcpy(e->msg,msg,ml+1);
		xSemaphoreGive(SyslogLock);
		xSemaphoreGive(SyslogSendSem);
	}
}


static void report_error(int s, const char *m)
{
	int errcode;
	uint32_t optlen = sizeof(int);
	int err = getsockopt(s, SOL_SOCKET, SO_ERROR, &errcode, &optlen);
	const char *errstr;
	if (err == -1) {
		if (LastErr == -1)
			return;
		errstr = "unable to determine error";
		LastErr = -1;
	} else {
		if (LastErr == errcode)
			return;
		errstr = strerror(errcode);
		LastErr = errcode;
	}
	if (errstr == 0)
		errstr = "unknown error";
	log_error(TAG,"%s: %s",m,errstr);
}


static void syslog(void *param)
{
	log_info(TAG,"starting...");
	char buf[BUF_SIZE];
	for (;;) {
		if (!Config.has_syslog_host()) {
			vTaskDelay(3000/portTICK_PERIOD_MS);
			continue;
		}
		wifi_wait();
		int sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
		if (sock == -1) {
			if (-2 != LastErr) {
				log_error(TAG,"unable to create socket");
				LastErr = -2;
			}
			vTaskDelay(3000/portTICK_PERIOD_MS);
			continue;
		}
		const char *hn = Config.nodename().c_str();
		size_t hs = Config.nodename().size();
		struct sockaddr_in sin, addr;
		int r;
		bzero(&sin,sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(SYSLOG_PORT);
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
		if (-1 == bind(sock,(struct sockaddr *)&sin,sizeof(sin)))
			report_error(sock,"bind failed");
		in_addr_t ip = resolve_hostname(Config.syslog_host().c_str());
		if ((ip == INADDR_NONE) || (ip == 0)) {
			if (-3 != LastErr) {
				log_error(TAG,"unable to resolve host %s",Config.syslog_host().c_str());
				LastErr = -3;
			}
			goto restart;
		}
		bzero(&addr,sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = ip;
		addr.sin_port = htons(SYSLOG_PORT);
		log_info(TAG,"ready");
		SyslogRunning = true;
		log_info(TAG,"Atrium version " VERSION);
		do {
			xSemaphoreTake(SyslogSendSem,portMAX_DELAY);
			syslog_elem *e = SyslogQ+SyslogOut;
			size_t n;
			if (e->tv.tv_sec < 1E6) {
				n = snprintf(buf,sizeof(buf),"<%d>1 - %.*s %s - - - %.*s"
					, 16 << 3 | (e->lvl+3)	// facility local use = 16, pri = (facility)<<3|serverity
					, hs
					, hn
					, e->a
					, e->ml
					, e->msg
					);
			} else {
				struct tm tm;
				gmtime_r(&e->tv.tv_sec,&tm);
				n = snprintf(buf,sizeof(buf),"<%d>1 %4u-%02u-%02uT%02u:%02u:%02u.%03lu %.*s %s - - - %.*s"
					, 16 << 3 | (e->lvl+3)	// facility local use = 16, pri = (facility)<<3|serverity
					, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday
					, tm.tm_hour, tm.tm_min, tm.tm_sec, e->tv.tv_usec/1000
					, hs
					, hn
					, e->a
					, e->ml
					, e->msg
					);
			}
			xSemaphoreGive(SyslogBufSem);
			r = sendto(sock,buf,n,0,(const struct sockaddr *) &addr,sizeof(addr));
			if (-1 == r)
				con_printf("syslog: send %s",strerror(errno));
			++SyslogOut;
			if (SyslogOut == sizeof(SyslogQ)/sizeof(SyslogQ[0]))
				SyslogOut = 0;
		} while (r != -1);
		SyslogRunning = false;
		report_error(sock,"send failed");
restart:
		close(sock);
		vTaskDelay(3000/portTICK_PERIOD_MS);
	}
}


#ifdef CONFIG_IDF_TARGET_ESP32
#define stack_size 4096
#else
#define stack_size 2048
#endif

int syslog_setup(void)
{
	SyslogLock = xSemaphoreCreateMutex();
	SyslogBufSem = xSemaphoreCreateCounting(sizeof(SyslogQ)/sizeof(SyslogQ[0]),sizeof(SyslogQ)/sizeof(SyslogQ[0]));
	SyslogSendSem = xSemaphoreCreateCounting(sizeof(SyslogQ)/sizeof(SyslogQ[0]),0);
	BaseType_t r = xTaskCreate(&syslog, "syslog", stack_size, NULL, 5, NULL);
	if (r != pdPASS) {
		log_error("syslog","unable to create task: %ld",(long)r);
		return 1;
	}
	return 0;
}


#endif // CONFIG_SYSLOG
