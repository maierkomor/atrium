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
#include "log.h"
#include "netsvc.h"
#ifndef IDF_VERSION
#include "versions.h"
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/uart.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
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


#if IDF_VERSION > 32
#define DRIVER_ARG 0,0
#else
#define DRIVER_ARG 0
#endif


#if defined WITH_FATFS || defined WITH_SPIFFS
#define HAVE_FS
#endif

#define ANSI_RED	"\033[0;31m"
#define ANSI_GREEN  	"\033[0;32m"
#define ANSI_YELLOW  	"\033[0;33m"
#define ANSI_BLUE  	"\033[0;34m"
#define ANSI_PURPLE  	"\033[0;35m"
#define ANSI_CYAN	"\033[0;36m"
#define ANSI_DEFAULT	"\033[0;00m"


void log_syslog(log_level_t lvl, const char *a, const char *msg, size_t ml);


static SemaphoreHandle_t UartLock;
static int8_t LogUart = -1;

#ifdef HAVE_FS
static SemaphoreHandle_t FileLock;
static int LogFile = -1;
#endif

#ifdef CONFIG_IDF_TARGET_ESP8266
static uint32_t Start = 0;
#endif

const char UartPrefix[][8] = {
	ANSI_RED	"E",
	ANSI_PURPLE	"W",
	ANSI_BLUE	"I",
	ANSI_GREEN	"D",
};


void con_print(const char *str)
{
#if CONFIG_CONSOLE_UART_NONE != 1
	if (LogUart == -1)
		return;
	con_write(str,strlen(str));
#endif
}


void con_printf(const char *f, ...)
{
#if CONFIG_CONSOLE_UART_NONE != 1
	if (LogUart == -1)
		return;
	char buf[128];
	va_list val;
	va_start(val,f);
	int n = vsnprintf(buf,sizeof(buf),f,val);
	va_end(val);
	if (n > 0) {
		if (n > sizeof(buf))
			n = sizeof(buf);
		con_write(buf,n);
	}
#endif
}


void con_write(const char *str, ssize_t s)
{
#if CONFIG_CONSOLE_UART_NONE != 1
	if ((LogUart == -1) || (s <= 0))
		return;
	xSemaphoreTake(UartLock,portMAX_DELAY);
	uart_write_bytes(LogUart,str,s);
	uart_write_bytes(LogUart,"\r\n",2);
	xSemaphoreGive(UartLock);
#endif
}


void log_setup()
{
#ifdef CONFIG_IDF_TARGET_ESP8266
	// hw_timer seems to get started late
	// TIMER_BASE_CLK's macro lacks guarding paranthesis...
	// Start = (uint64_t)hw_timer_get_count_data()*1000ULL/(TIMER_BASE_CLK);
	Start = (uint32_t)((uint64_t)soc_get_ccount()*1000ULL/(uint64_t)(CPU_CLK_FREQ));
#endif

	UartLock = xSemaphoreCreateMutex();

#if CONFIG_UART_CONSOLE_NONE == 1
	LogUart = -1;
#else
	LogUart = -1;
	//LogUart = (uart_port_t) CONFIG_CONSOLE_UART_NUM;
	//uart_driver_install(LogUart,UART_FIFO_LEN*2,UART_FIFO_LEN*2,0,DRIVER_ARG);
#endif

#ifdef HAVE_FS
	FileLock = xSemaphoreCreateMutex();
#endif
}


void log_set_uart(int8_t uart)
{
	xSemaphoreTake(UartLock,portMAX_DELAY);
	LogUart = uart;
	xSemaphoreGive(UartLock);
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


static inline char *append(char *to, const char *from)
{
	char c;
	while ((c = *from++) != 0)
		*to++ = c;
	//*to = 0;	-- we omit \0, as more text will follow
	return to;
}


#ifdef HAVE_FS
static void log_file(log_level_t lvl, const char *m, size_t l)
{
	xSemaphoreTake(FileLock,portMAX_DELAY);
	write(LogFile,m,ml+2);
	xSemaphoreGive(FileLock);
}
#endif


static void log_uart(log_level_t lvl, const char *m, size_t ml)
{
	if (LogUart == -1)
		return;
	xSemaphoreTake(UartLock,portMAX_DELAY);
	uart_write_bytes((uart_port_t)LogUart,m,ml);
	xSemaphoreGive(UartLock);
	if ((lvl == ll_error) || (lvl == ll_warn))
#if IDF_VERSION >= 32
		uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
#else
		uart_wait_tx_done((uart_port_t)LogUart);
#endif
}


static int write_prefix(log_level_t lvl, const char *a, char *buf, size_t n)
{
	memcpy(buf,UartPrefix[lvl],8);
	char *at = buf + 8;
	*at++ = ' ';
	int r = 9;
	n -= r;
	struct timeval tv;
	if ((-1 == gettimeofday(&tv,0)) || (tv.tv_sec < 1E6)) {
#ifdef CONFIG_IDF_TARGET_ESP8266
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_NORMAL
		r += snprintf(at,n,"(%6f) %s: ",(double)((float)clock()/(float)CLOCKS_PER_SEC+(float)Start),a);
#else
		r += snprintf(at,n,"(%6lu) %s: ",clock()*1000/CLOCKS_PER_SEC+Start,a);
#endif
#else
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME,&ts);
		r += snprintf(at,n,"(%lu.%03u) %s: ",ts.tv_sec,(unsigned)(ts.tv_nsec/1E6),a);
#endif
	} else {
		struct tm tm;
		localtime_r(&tv.tv_sec,&tm);
		r += snprintf(at,n
			, "%02d:%02d:%02d.%03lu %s: "
			, (int)tm.tm_hour, (int)tm.tm_min, (int)tm.tm_sec, tv.tv_usec/1000, a);
	}
	return r;
}


void log_common(log_level_t l, const char *a, const char *f, va_list val)
{
	char buf[LOG_MAXLEN+1];
	int p = write_prefix(l,a,buf,sizeof(buf));
	if (p >= sizeof(buf))
		return;
	int s = vsnprintf(buf+p,sizeof(buf)-p,f,val);
	if (s <= 0)
		return;
	char *out = buf;
	if (p+s+2 > sizeof(buf)) {
		buf[sizeof(buf)-1] = '\n';
		buf[sizeof(buf)-2] = '\r';
		buf[sizeof(buf)-3] = ']';
		buf[sizeof(buf)-4] = '.';
		buf[sizeof(buf)-5] = '.';
		buf[sizeof(buf)-6] = '.';
		buf[sizeof(buf)-7] = '[';
		s = sizeof(buf)-p-2;
	} else {
		buf[p+s] = '\r';
		buf[p+s+1] = '\n';
	}
	log_uart(l,buf,s+p+2);
#ifdef HAVE_FS
	if (LogFile == -1)
		log_file(out+7,s+p-7+2);
#endif
#ifdef CONFIG_SYSLOG
	log_syslog(l,a,out+p,s);
#endif
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
	uint32_t t = esp_timer_get_time() / 1000;
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


