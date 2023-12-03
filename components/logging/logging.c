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

#if defined CONFIG_USB_CONSOLE || defined CONFIG_USB_DIAGLOG
#include <driver/usb_serial_jtag.h>
#endif

#if 0 // defined CONFIG_USB_CONSOLE && defined CONFIG_TINYUSB_CDC_ENABLED
#include <tusb_cdc_acm.h>
#include <tusb_console.h>
#include <tusb_tasks.h>
#include <tinyusb_types.h>
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

#ifndef CONFIG_CONSOLE_UART_RX
#define CONFIG_CONSOLE_UART_RX -1
#endif

#ifndef CONFIG_CONSOLE_UART_TX
#define CONFIG_CONSOLE_UART_TX -1
#endif


#define ANSI_RED	"\033[0;31m"
#define ANSI_GREEN  	"\033[0;32m"
#define ANSI_YELLOW  	"\033[0;33m"
#define ANSI_BLUE  	"\033[0;34m"
#define ANSI_PURPLE  	"\033[0;35m"
#define ANSI_CYAN	"\033[0;36m"
#define ANSI_DEFAULT	"\033[0;00m"

extern void log_usb(const char *, size_t n);

uint8_t UsbDiag = 1;
static SemaphoreHandle_t UartLock;
#if CONFIG_CONSOLE_UART_NONE != 1
static uart_port_t LogUart = (uart_port_t) CONFIG_CONSOLE_UART_NUM;
#else
static uart_port_t LogUart = (uart_port_t) -1;
#endif

#ifdef HAVE_FS
static SemaphoreHandle_t FileLock = 0;
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
	if (str == 0)
		return;
	size_t s = strlen(str);
#if CONFIG_CONSOLE_UART_NONE != 1
	if ((LogUart != -1) && (s != 0)) {
		if (pdFALSE == xSemaphoreTake(UartLock,MUTEX_ABORT_TIMEOUT))
			abort_on_mutex(UartLock,__FUNCTION__);
		uart_write_bytes(LogUart,str,s);
		uart_write_bytes(LogUart,"\r\n",2);
		uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
		xSemaphoreGive(UartLock);
	}
#endif
#if defined CONFIG_USB_DIAGLOG
	// will block until a jtag connection is present
	// therefore, max delay: 10ms
	usb_serial_jtag_write_bytes(str,s,10);
	usb_serial_jtag_write_bytes("\r\n",2,10);
#endif
}


void con_printf(const char *f, ...)
{
	va_list val;
	va_start(val,f);
	con_printv(f,val);
	va_end(val);
}


void con_printv(const char *f, va_list val) 
{
	char buf[256];
	int n = vsnprintf(buf,sizeof(buf),f,val);
	if (n > 0) {
		if (n > sizeof(buf))
			n = sizeof(buf);
#if CONFIG_CONSOLE_UART_NONE != 1
		if (LogUart != -1) {
			if (pdFALSE == xSemaphoreTake(UartLock,MUTEX_ABORT_TIMEOUT))
				abort_on_mutex(UartLock,__FUNCTION__);
			uart_write_bytes(LogUart,buf,n);
			uart_write_bytes(LogUart,"\r\n",2);
			uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
			xSemaphoreGive(UartLock);
		}
#endif
#ifdef CONFIG_USB_DIAGLOG
		usb_serial_jtag_write_bytes(buf,n,portMAX_DELAY);
#endif
	}
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
#ifdef CONFIG_USB_DIAGLOG
	usb_serial_jtag_write_bytes(str,s,portMAX_DELAY);
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
#if CONFIG_UART_CONSOLE_NONE != 1 && CONFIG_CONSOLE_UART_NUM != -1
	uart_driver_install((uart_port_t)CONFIG_CONSOLE_UART_NUM,UART_FIFO_LEN*2,UART_FIFO_LEN*2,0,DRIVER_ARG);
#if (CONFIG_CONSOLE_UART_RX != -1) || (CONFIG_CONSOLE_UART_TX != -1)
	uart_set_pin((uart_port_t)CONFIG_CONSOLE_UART_NUM, CONFIG_CONSOLE_UART_TX, CONFIG_CONSOLE_UART_RX , -1, -1);
#endif
#endif

#if defined CONFIG_USB_DIAGLOG || defined CONFIG_USB_CONSOLE
	usb_serial_jtag_driver_config_t cfg;
	bzero(&cfg,sizeof(cfg));
	cfg.rx_buffer_size = 256;
	cfg.tx_buffer_size = 768;
	esp_err_t e = usb_serial_jtag_driver_install(&cfg);
	if (e)
		con_printf("jtag init: %s",esp_err_to_name(e));
#endif
#if 0 //def CONFIG_ESP_CONSOLE_USB_CDC
#endif
#if 0 // defined CONFIG_USB_CONSOLE && defined CONFIG_TINYUSB_CDC_ENABLED
	esp_tusb_init_console(TINYUSB_CDC_ACM_0);
	tinyusb_config_t usbcfg;
	bzero(&usbcfg,sizeof(usbcfg));
	tinyusb_driver_install(&usbcfg);
	tinyusb_config_cdcacm_t acmcfg;
	bzero(&acmcfg,sizeof(acmcfg));
	acmcfg.usb_dev = TINYUSB_USBDEV_0;
	acmcfg.cdc_port = TINYUSB_CDC_ACM_0;
	acmcfg.rx_unread_buf_sz = 64;
	//acmcfg.callback_rx = tusb_cdc_rx_cb;
	acmcfg.callback_rx_wanted_char = 0;
	acmcfg.callback_line_state_changed = 0;
	acmcfg.callback_line_coding_changed = 0;
	tusb_cdc_acm_init(&acmcfg);
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
	if (FileLock == 0)
		FileLock = xSemaphoreCreateMutex();
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
		p += sprintf(at," (%llu.%03u) %s: ",ts.tv_sec,(unsigned)(ts.tv_nsec/1E6),a);
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
			abort_on_mutex(UartLock,__BASE_FILE__);
		uart_write_bytes((uart_port_t)LogUart,buf,s);
#ifndef CONFIG_DEVEL
		if (l <= ll_warn)
#endif
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
#ifdef CONFIG_USB_DIAGLOG
	if (UsbDiag)
		usb_serial_jtag_write_bytes(buf,s,0);
#endif
#if 0 // defined CONFIG_USB_CONSOLE && defined CONFIG_TINYUSB_CDC_ENABLED
//	Logging to CDC would only work if driver is ready.
//	This is currently not supported, as the S2 CDC drivers stalls
//	after startup.
//	(void) tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,(const uint8_t *)buf,s);
//	(void) tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0,100); //portMAX_DELAY);
#endif
}


void log_direct(log_level_t ll, logmod_t m, const char *f, ...)
{
	va_list val;
	va_start(val,f);
	log_common(ll,m,f,val);
	va_end(val);
}


void log_directv(log_level_t ll, logmod_t m, const char *f, va_list val)
{
	log_common(ll,m,f,val);
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


void uart_print(const char *str)
{
	uart_write_bytes(LogUart,str,strlen(str));
	uart_write_bytes(LogUart,"\r\n",2);
}


void abort_on_mutex(SemaphoreHandle_t mtx, const char *usage)
{
	uart_print(usage ? usage : "<null>");
	uart_print(__FUNCTION__);
	TaskHandle_t h = xSemaphoreGetMutexHolder(mtx);
#if defined ESP32
	const char *n = pcTaskGetName(0);
	if (n)
		uart_print(n);
	n = pcTaskGetName(h);
	if (n)
		uart_print(n);
#else
	uart_print(pcTaskGetName(0));
	uart_print(pcTaskGetName(h));
//	uart_printf("%s: task %s hanging on task %s\n",usage,pcTaskGetName(0),pcTaskGetName(h));
#endif
	uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
#if 0
//#if defined CONFIG_FREERTOS_USE_TRACE_FACILITY && defined CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
	char buf[512];
	vTaskList(buf);
	con_print(buf);
	uart_wait_tx_done((uart_port_t)LogUart,portMAX_DELAY);
#endif
	abort();
}
