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
#include <freertos/task.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/uart.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
#include <esp8266/gpio_register.h>
#include <esp8266/timer_register.h>
#include <driver/hw_timer.h>
#endif

#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if defined WITH_FATFS || defined WITH_SPIFFS
#define HAVE_FS
#include <fcntl.h>
#include <stdio.h>
#endif

#if IDF_VERSION > 32
#define DRIVER_ARG 0,0
#else
#define DRIVER_ARG 0
#endif

#if CONFIG_UART_CONSOLE_NONE != 1
#ifndef CONFIG_CONSOLE_UART_NUM
#define CONFIG_CONSOLE_UART_NUM 0
#endif
#endif

#define ANSI_RED	"\033[0;31m"
#define ANSI_GREEN  	"\033[0;32m"
#define ANSI_YELLOW  	"\033[0;33m"
#define ANSI_BLUE  	"\033[0;34m"
#define ANSI_PURPLE  	"\033[0;35m"
#define ANSI_CYAN	"\033[0;36m"
#define ANSI_DEFAULT	"\033[0;00m"


static SemaphoreHandle_t UartLock;
#if CONFIG_CONSOLE_UART_NONE != 1
static uart_port_t LogUart = (uart_port_t) CONFIG_CONSOLE_UART_NUM;
#else
static uart_port_t LogUart = (uart_port_t) -1;
#endif

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
	ANSI_CYAN	"L",
};


void con_print(const char *str)
{
#if CONFIG_CONSOLE_UART_NONE != 1
	if ((LogUart != -1) && (str != 0)) {
		size_t s = strlen(str);
		if (pdFALSE == xSemaphoreTake(UartLock,MUTEX_ABORT_TIMEOUT))
			abort_on_mutex(UartLock,__FUNCTION__);
		uart_write_bytes(LogUart,str,s);
		uart_write_bytes(LogUart,"\r\n",2);
		xSemaphoreGive(UartLock);
	}
#endif
}


void con_printf(const char *f, ...)
{
#if CONFIG_CONSOLE_UART_NONE != 1
	if (LogUart == -1)
		return;
	char buf[256];
	va_list val;
	va_start(val,f);
	int n = vsnprintf(buf,sizeof(buf),f,val);
	va_end(val);
	if (n > 0) {
		if (n > sizeof(buf))
			n = sizeof(buf);
		con_write(buf,n);
	}
//	uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
#endif
}


void con_write(const char *str, ssize_t s)
{
#if CONFIG_CONSOLE_UART_NONE != 1
	if ((LogUart != -1) && (s > 0)) {
		xSemaphoreTake(UartLock,portMAX_DELAY);
		uart_write_bytes(LogUart,str,s);
		xSemaphoreGive(UartLock);
	}
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
	uart_driver_install((uart_port_t)CONFIG_CONSOLE_UART_NUM,UART_FIFO_LEN*2,UART_FIFO_LEN*2,0,DRIVER_ARG);
#endif

#ifdef HAVE_FS
	FileLock = xSemaphoreCreateMutex();
#endif
}


void log_set_uart(int8_t uart)
{
	xSemaphoreTake(UartLock,portMAX_DELAY);
	LogUart = (uart_port_t)uart;
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


void log_common(log_level_t l, logmod_t m, const char *f, va_list val)
{
	char buf[32+LOG_MAXLEN+1];	// 32 for the prefix
	char *at = buf + 8;
	int p = 8;
	struct timeval tv;
	const char *a = ModNames+ModNameOff[m];
	if ((-1 == gettimeofday(&tv,0)) || (tv.tv_sec < 1E6)) {
#ifdef CONFIG_IDF_TARGET_ESP8266
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_NORMAL
		p += sprintf(at," (%6f) %s: ",(double)((float)clock()/(float)CLOCKS_PER_SEC+(float)Start),a);
#else
		p += sprintf(at," (%6lu) %s: ",clock()*1000/CLOCKS_PER_SEC+Start,a);
#endif
#else
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME,&ts);
		p += sprintf(at," (%lu.%03u) %s: ",ts.tv_sec,(unsigned)(ts.tv_nsec/1E6),a);
#endif
	} else {
		struct tm tm;
		localtime_r(&tv.tv_sec,&tm);
		p += sprintf(at, " %02d:%02d:%02d.%03lu %s: "
			, (int)tm.tm_hour, (int)tm.tm_min, (int)tm.tm_sec, tv.tv_usec/1000,a);
	}
	int s = vsnprintf(buf+p,sizeof(buf)-p,f,val);
	if (s <= 0)
		return;
	memcpy(buf,UartPrefix[l],8);
	s += p;
	if (s+2 > sizeof(buf)) {
		buf[sizeof(buf)-3] = ']';
		buf[sizeof(buf)-4] = '.';
		buf[sizeof(buf)-5] = '.';
		buf[sizeof(buf)-6] = '.';
		buf[sizeof(buf)-7] = '[';
		s = sizeof(buf)-2;
	}
	buf[s++] = '\r';
	buf[s++] = '\n';
	if (LogUart >= 0) {
		if (pdTRUE != xSemaphoreTake(UartLock,MUTEX_ABORT_TIMEOUT))
			abort_on_mutex(UartLock,__FILE__);
		uart_write_bytes((uart_port_t)LogUart,buf,s);
		if (l <= ll_warn)
			uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
		xSemaphoreGive(UartLock);
	}
#ifdef HAVE_FS
	if (LogFile >= 0) {
		xSemaphoreTake(FileLock,portMAX_DELAY);
		write(LogFile,buf+9,s-9);
		xSemaphoreGive(FileLock);
	}
#endif
#ifdef CONFIG_SYSLOG
	if ((l != ll_local) && (m != MODULE_LOG) && (m != MODULE_LWTCP))
		log_syslog(l,m,buf+p,s-p-2,&tv);
#endif
}


void log_direct(log_level_t ll, logmod_t m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll,m,f,val);
	va_end(val);
}


void log_fatal(logmod_t m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll_error,m,f,val);
	va_end(val);
	abort();
}


void log_error(logmod_t m, const char *f, ...)
{
	static uint32_t ts = 0;
	static uint8_t cnt = 0;
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


void log_warn(logmod_t m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll_warn,m,f,val);
	va_end(val);
}


void log_info(logmod_t m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll_info,m,f,val);
	va_end(val);
}


static inline void uart_print(const char *str)
{
	uart_write_bytes(LogUart,str,strlen(str));
	uart_write_bytes(LogUart,"\r\n",2);
}


void abort_on_mutex(SemaphoreHandle_t mtx, const char *usage)
{
	uart_print(usage ? usage : "<null>");
	uart_print(__FUNCTION__);
	TaskHandle_t h = xSemaphoreGetMutexHolder(mtx);
#ifdef CONFIG_IDF_TARGET_ESP32
	uart_print(pcTaskGetTaskName(0));
	uart_print(pcTaskGetTaskName(h));
#else
	uart_print(pcTaskGetName(0));
	uart_print(pcTaskGetName(h));
//	uart_printf("%s: task %s hanging on task %s\n",usage,pcTaskGetName(0),pcTaskGetName(h));
#endif
	uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
	char buf[512];
	vTaskList(buf);
	con_print(buf);
	uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
	abort();
}
