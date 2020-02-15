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
#include "log.h"
#include "globals.h"
#include "mqtt.h"
#include "support.h"
#include "terminal.h"
#include "wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/uart.h>

#include <lwip/udp.h>
#include <sys/socket.h>

#ifdef ESP8266
#include <esp8266/gpio_register.h>
#include <esp8266/timer_register.h>
#include <driver/hw_timer.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <vector>

using namespace std;

#if defined WITH_FATFS || defined WITH_SPIFFS
#define HAVE_FS
#endif

#define ANSI_RED	"\033[0;31m"
#define ANSI_GREEN  	"\033[0;32m"
#define ANSI_YELLOW  	"\033[0;33m"
#define ANSI_BLUE  	"\033[0;34m"
#define ANSI_PURPLE  	"\033[0;35m"
#define ANSI_CYAN	"\033[0;36m"
#define ANSI_DEFAULT	"\033[0;0m"

SemaphoreHandle_t UartLock, DmesgLock;

#ifdef HAVE_FS
SemaphoreHandle_t FileLock;
static int LogFile = -1;
#endif

#if CONFIG_DMESG_SIZE > 0
char DMesgBuf[CONFIG_DMESG_SIZE];
char *DMesgAt;
#endif

#ifdef ESP32
#elif defined ESP8266
static uint32_t Start = 0;

#endif

uint32_t Debug = UINT32_MAX;


#ifdef CONFIG_SYSLOG
#define SYSLOG_PORT 514
static SemaphoreHandle_t SyslogLock, SyslogBufSem, SyslogSendSem;
static char *SyslogQ[16];
static uint8_t SyslogIn = 0, SyslogOut = 0;
static bool SyslogRunning = false;
#endif

const char UartPrefix[][12] = {
	ANSI_RED	"E ",
	ANSI_PURPLE	"W ",
	ANSI_BLUE	"I ",
	ANSI_GREEN	"D ",
};



void con_print(const char *str)
{
	xSemaphoreTakeRecursive(UartLock,portMAX_DELAY);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,str,strlen(str));
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,"\r\n",2);
	xSemaphoreGiveRecursive(UartLock);
}


void con_printf(const char *f, ...)
{
	char buf[128];
	va_list val;
	va_start(val,f);
	int n = vsnprintf(buf,sizeof(buf),f,val);
	va_end(val);
	xSemaphoreTakeRecursive(UartLock,portMAX_DELAY);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,buf,n);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,"\r\n",2);
	xSemaphoreGiveRecursive(UartLock);
}


void con_write(const char *str,size_t s)
{
	xSemaphoreTakeRecursive(UartLock,portMAX_DELAY);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,str,s);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,"\r\n",2);
	xSemaphoreGiveRecursive(UartLock);
}


void log_setup()
{
#ifdef ESP8266
	// hw_timer seems to get started late
	// TIMER_BASE_CLK's macro lacks guarding paranthesis...
	// Start = (uint64_t)hw_timer_get_count_data()*1000ULL/(TIMER_BASE_CLK);
	Start = (uint32_t)((uint64_t)soc_get_ccount()*1000ULL/(uint64_t)(CPU_CLK_FREQ));
#endif
	UartLock = xSemaphoreCreateRecursiveMutex();

#ifdef HAVE_FS
	FileLock = xSemaphoreCreateMutex();
#endif
#if CONFIG_DMESG_SIZE > 0
	DmesgLock = xSemaphoreCreateMutex();
	DMesgAt = DMesgBuf;
#endif
}


#ifdef HAVE_FS
void set_logfile(const char *fn)
{
	if (LogFile != -1)
		close(LogFile);
	LogFile = open(fn,O_WRONLY|O_CREAT,0666);
	if (LogFile == -1)
		log_error("log","unable to open logfile %s: %s",fn,strerror(errno));
}
#endif


static int write_timestamp(char *buf, size_t n)
{
	struct timeval tv;
	if ((-1 == gettimeofday(&tv,0)) || (tv.tv_sec < 1E6)) {
#ifdef ESP8266
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_NORMAL
		return snprintf(buf,n,"(%6f) ",(double)((float)clock()/(float)CLOCKS_PER_SEC+(float)Start));
#else
		return snprintf(buf,n,"(%6lu) ",clock()*1000/CLOCKS_PER_SEC+Start);
#endif
#else
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME,&ts);
		return snprintf(buf,n,"(%lu.%03u) ",ts.tv_sec,(unsigned)(ts.tv_nsec/1E6));
#endif
	}
	struct tm tm;
	localtime_r(&tv.tv_sec,&tm);
	return snprintf(buf,n
			,"%4u-%02u-%02uT%02u:%02u:%02u.%03lu "
			,tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour
			,tm.tm_min,tm.tm_sec,tv.tv_usec/1000);
}


static inline char *append(char *to, const char *from)
{
	while (char c = *from++)
		*to++ = c;
	//*to = 0;	-- we omit \0, as more text will follow
	return to;
}


#ifdef HAVE_FS
static void log_file(log_level_t lvl, const char *ts, const char *a, const char *m, size_t l)
{
	if (LogFile == -1)
		return;
	xSemaphoreTake(FileLock,portMAX_DELAY);
	write(LogFile,UartPrefix[lvl],9);
	write(LogFile,ts,l);
	write(LogFile,a,strlen(a));
	write(LogFile,": ",2);
	write(LogFile,m,ml);
	write(LogFile,"\r\n",2);
	xSemaphoreGive(FileLock);
}
#endif


#ifdef CONFIG_SYSLOG
static void log_syslog(log_level_t lvl, const char *ts, const char *a, const char *msg, size_t ml)
{
	// header: pri version timestamp hostname app-name procid msgid
	if ((strlen(ts) < 11) || (ts[10] != 'T'))
		ts = "- ";
	size_t n = 6+1 // pri+ver+sp
		+ 24+1 // ts+sp
		+ Config.nodename().size() + 1 
		+ strlen(a) + 1 // app
		+ 2 // proc-id
		+ 2 // msg-id
		+ 2 // structured-data-nil
		+ ml + 1;
	char *p = (char *) malloc(n);
	int s = snprintf(p,n,
		"<%d>1 %s%s %s - - - "
		, 16 << 3 | (lvl+3)	// facility local use = 16, pri = (facility)<<3|serverity
		, ts
		, Config.nodename().c_str()
		, a
		);
	assert(s+ml+1<=n);
	memcpy(p+s,msg,ml+1);
	if (pdFALSE == xSemaphoreTake(SyslogBufSem,100/portTICK_PERIOD_MS))
		return;
	xSemaphoreTake(SyslogLock,portMAX_DELAY);
	SyslogQ[SyslogIn++] = p;
	if (SyslogIn == sizeof(SyslogQ)/sizeof(SyslogQ[0]))
		SyslogIn = 0;
	xSemaphoreGive(SyslogLock);
	xSemaphoreGive(SyslogSendSem);
}
#endif

static void log_uart(log_level_t lvl, const char *ts, const char *a, const char *m, size_t ml)
{
	size_t l;
	if (ts[10] == 'T') {
		ts += 11;
		l = 14;
	} else {
		char *p = strchr(ts,')');
		if (p)
			l = p-ts+2;
		else
			l = strlen(ts);
	}
	//assert(l);
	xSemaphoreTakeRecursive(UartLock,portMAX_DELAY);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,UartPrefix[lvl],9);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,ts,l);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,a,strlen(a));
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,": ",2);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,m,ml);
	uart_write_bytes((uart_port_t)CONFIG_CONSOLE_UART_NUM,"\r\n",2);
	xSemaphoreGiveRecursive(UartLock);
	if ((lvl == ll_error) || (lvl == ll_warn))
#if IDF_VERSION >= 32
		uart_wait_tx_done((uart_port_t)CONFIG_CONSOLE_UART_NUM,portMAX_DELAY);
#else
		uart_wait_tx_done((uart_port_t)CONFIG_CONSOLE_UART_NUM);
#endif
}


#if CONFIG_DMESG_SIZE > 0
static void log_dmesg(const char *a, const char *m, size_t ml)
{
	size_t al = strlen(a);
	size_t l = al+ml+4;
	if (l > (sizeof(DMesgBuf)>>2))
		return;
	xSemaphoreTake(DmesgLock,portMAX_DELAY);
	while (DMesgAt+l+2 >= DMesgBuf+sizeof(DMesgBuf)) {
		char *nl = (char*)memchr(DMesgBuf,'\n',DMesgAt-DMesgBuf);
		if (nl == 0) {
			xSemaphoreGive(DmesgLock);
			return;
		}
		++nl;
		memmove(DMesgBuf,nl,DMesgAt-nl);
		DMesgAt -= (nl-DMesgBuf);
	}
	memcpy(DMesgAt,a,al);
	DMesgAt += al;
	*DMesgAt++ = ':';
	*DMesgAt++ = ' ';
	memcpy(DMesgAt,m,ml);
	DMesgAt += ml;
	*DMesgAt++ = '\r';
	*DMesgAt++ = '\n';
	xSemaphoreGive(DmesgLock);
}


extern int dmesg(Terminal &term, int argc, const char *args[])
{
#ifdef write
#undef write
#endif
	xSemaphoreTake(DmesgLock,portMAX_DELAY);
	term.write(DMesgBuf,DMesgAt-DMesgBuf);
	xSemaphoreGive(DmesgLock);
	return 0;
}
#endif


void log_common(log_level_t l, const char *a, const char *f, va_list val)
{
	char buf[120], ts[25];
	int s = vsnprintf(buf,sizeof(buf),f,val);
	if (s <= 0)
		return;
	write_timestamp(ts,sizeof(ts));
	char *out = buf;
	if (s > sizeof(buf)) {
		char *tmp = (char *)malloc(s+1);
		if (tmp == 0) {
			buf[sizeof(buf)-1] = ']';
			buf[sizeof(buf)-2] = '.';
			buf[sizeof(buf)-3] = '.';
			buf[sizeof(buf)-4] = '.';
			buf[sizeof(buf)-5] = '[';
			s = sizeof(buf);
		} else {
			vsprintf(tmp,f,val);
			out = tmp;
		}
	}
	log_uart(l,ts,a,out,s);
#if CONFIG_DMESG_SIZE > 0
	log_dmesg(a,out,s);
#endif
	if (l != ll_debug) {
#ifdef HAVE_FS
		log_file(l,ts,a,out,s);
#endif
#ifdef CONFIG_SYSLOG
		if (SyslogRunning && (l != ll_debug))
			log_syslog(l,ts,a,out,s);
#endif
#ifdef CONFIG_MQTT
		mqtt_set_dmesg(out,s);
#endif
	}
	if (out != buf)
		free(out);
}


void log_local(log_level_t l, const char *a, const char *f, va_list val)
{
	char buf[120], ts[25];
	int s = vsnprintf(buf,sizeof(buf),f,val);
	if (s <= 0)
		return;
	write_timestamp(ts,sizeof(ts));
	char *out = buf;
	if (s > sizeof(buf)) {
		char *tmp = (char *)malloc(s+1);
		if (tmp == 0) {
			buf[sizeof(buf)-1] = ']';
			buf[sizeof(buf)-2] = '.';
			buf[sizeof(buf)-3] = '.';
			buf[sizeof(buf)-4] = '.';
			buf[sizeof(buf)-5] = '[';
			s = sizeof(buf);
		} else {
			vsprintf(tmp,f,val);
			out = tmp;
		}
	}
	log_uart(l,ts,a,out,s);
	if (l != ll_debug) {
#if CONFIG_DMESG_SIZE > 0
		log_dmesg(a,out,s);
#endif
	}
	if (out != buf)
		free(out);
}



void log_fatal(const char *m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll_error,m,f,val);
	va_end(val);
	abort();
}


void log_error(const char *m, const char *f, ...)
{
	static uint32_t ts = 0;
	static unsigned cnt = 0;
	bool fatal = false;
	uint32_t t = timestamp();
	if (t - ts > 2000000) {
		ts = t;
		cnt = 0;
	} else if (++cnt > 10) 
		fatal = true;
	va_list val;
	va_start(val,f);
	log_common(ll_error,m,f,val);
	va_end(val);
	if (fatal)
		abort();
}


void log_warn(const char *m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll_warn,m,f,val);
	va_end(val);
}


void log_info(const char *m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll_info,m,f,val);
	va_end(val);
}


void log_dbug(const char *m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll_debug,m,f,val);
	va_end(val);
}


#ifdef CONFIG_SYSLOG

static int LastErr = 0;

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
	con_printf("rlog: %s: %s",m,errstr);
}


static void syslog(void *param)
{
	in_addr_t ip = INADDR_NONE;
	con_printf("rlog: starting...");
	for (;;) {
		wifi_wait();
		int sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
		if (sock == -1) {
			if (-2 != LastErr) {
				con_printf("rlog: unable to create socket");
				LastErr = -2;
			}
			vTaskDelay(3000/portTICK_PERIOD_MS);
			continue;
		}
		struct sockaddr_in sin;
		memset(&sin,0,sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(514);
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
		if (-1 == bind(sock,(struct sockaddr *)&sin,sizeof(sin)))
			report_error(sock,"bind failed");
		if (ip == INADDR_NONE) {
			ip = resolve_hostname(Config.syslog_host().c_str());
			if (ip == INADDR_NONE) {
				if (-3 != LastErr) {
					con_printf("rlog: unable to resolve host %s",Config.syslog_host().c_str());
					LastErr = -3;
				}
				vTaskDelay(3000/portTICK_PERIOD_MS);
				continue;
			}
		}
		struct sockaddr_in addr;
		memset(&addr,0,sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = ip;
		addr.sin_port = htons(SYSLOG_PORT);
		con_printf("rlog: ready");
		SyslogRunning = true;
		int r;
		do {
			xSemaphoreTake(SyslogSendSem,portMAX_DELAY);
			char *buf = SyslogQ[SyslogOut];
			++SyslogOut;
			if (SyslogOut == sizeof(SyslogQ)/sizeof(SyslogQ[0]))
				SyslogOut = 0;
			xSemaphoreGive(SyslogBufSem);
			r = sendto(sock,buf,strlen(buf),0,(const struct sockaddr *) &addr,sizeof(addr));
			if (-1 == r)
				con_printf("syslog: send %s",strerror(errno));
			free(buf);
		} while (r != -1);
		SyslogRunning = false;
		report_error(sock,"send failed");
		close(sock);
		vTaskDelay(3000/portTICK_PERIOD_MS);
	}
}



extern "C"
void syslog_setup(void)
{
	if (!Config.has_syslog_host())
		return;
	SyslogLock = xSemaphoreCreateMutex();
	SyslogBufSem = xSemaphoreCreateCounting(sizeof(SyslogQ)/sizeof(SyslogQ[0]),sizeof(SyslogQ)/sizeof(SyslogQ[0]));
	SyslogSendSem = xSemaphoreCreateCounting(sizeof(SyslogQ)/sizeof(SyslogQ[0]),0);
	BaseType_t r = xTaskCreate(&syslog, "syslog", 2048, NULL, 5, NULL);
	if (r == pdPASS) {
		log_info("syslog","task started");
	} else {
		log_error("syslog","unable to create task: %ld",(long)r);
	}
}

#endif // CONFIG_SYSLOG
