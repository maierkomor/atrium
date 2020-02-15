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

#include "uart_terminal.h"
#include "globals.h"
#include "log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <rom/gpio.h>
extern "C" {
#include <esp_task_wdt.h>
}

#include <stdarg.h>
#include <stdio.h>

#include <atomic>

using namespace std;


static char TAG[] = "uarttty";

#if IDF_VERSION > 32
#define DRIVER_ARG 0,0
#else
#define DRIVER_ARG 0
#endif

#if CONFIG_TERMSERV == 1

#ifdef ESP8266
// ESP8266 has only one UART/rx
// uart0/rx: gpio3/pin25
// uart0/tx: gpio1/pin26
// uart1/tx: gpio2/pin14
#define CONFIG_TERMSERV_RX 0
#if (CONFIG_CONSOLE_UART_NONE == 0) && (CONFIG_CONSOLE_UART_NUM == CONFIG_TERMSERV_UART)
#error terminal and console require different uart
#endif

#else //ESP32
#if (CONFIG_TERMSERV_UART == CONFIG_CONSOLE_UART_NUM) && (CONFIG_CONSOLE_UART_NONE == 0)
#error terminal and console require different uart
#endif
#define CONFIG_TERMSERV_RX CONFIG_TERMSERV_UART
#endif

#define CONFIG_TERMSERV_TX CONFIG_TERMSERV_UART

void termserv_setup()
{
// On ESP8266 only UART0 has rx/tx, UART1 only provides tx
#ifdef ESP32
	if (esp_err_t e = uart_driver_install((uart_port_t)CONFIG_TERMSERV_UART,UART_FIFO_LEN*8,UART_FIFO_LEN*8,0,0,0))
		log_error(TAG,"failed to init terminal server uart %s",esp_err_to_name(e));
	else
		log_info(TAG,"terminal server on uart %u",CONFIG_TERMSERV_UART);

#elif defined ESP8266
	if (esp_err_t e = uart_driver_install((uart_port_t)0,UART_FIFO_LEN*2,UART_FIFO_LEN*8,0,DRIVER_ARG))
		log_warn(TAG,"failed to init terminal server uart0 %s",esp_err_to_name(e));
	else
		log_info(TAG,"terminal server RX on uart 0");
#if CONFIG_TERMSERV_UART == 1
	if (esp_err_t e = uart_driver_install((uart_port_t)1,UART_FIFO_LEN*2,UART_FIFO_LEN*8,0,DRIVER_ARG))
		log_error(TAG,"failed to init terminal server uart1 %s",esp_err_to_name(e));
	else
		log_info(TAG,"terminal server TX on uart 1");
#endif
#endif

#ifdef ESP8266
#if CONFIG_TERMSERV_TX != CONFIG_TERMSERV_RX
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
	if (esp_err_t e = uart_set_baudrate((uart_port_t)CONFIG_TERMSERV_RX,CONFIG_TERMSERV_BAUDRATE))
		log_error(TAG,"failed to set baudrate for terminal server uart/tx %s",esp_err_to_name(e));
#endif
#endif
	if (esp_err_t e = uart_set_baudrate((uart_port_t)CONFIG_TERMSERV_UART,CONFIG_TERMSERV_BAUDRATE))
		log_error(TAG,"failed to set baudrate for terminal server uart/tx %s",esp_err_to_name(e));
}
#endif // CONFIG_TERMSERV


UartTerminal::UartTerminal(uint8_t uart, bool crnl, uint16_t delay)
: Terminal(crnl)
, m_delay(delay)
, m_uart(uart)
{
}


UartTerminal::~UartTerminal()
{
	if (m_uart == CONFIG_CONSOLE_UART_NUM)
		return;
	if (ESP_OK != uart_driver_delete((uart_port_t)m_uart))
		log_warn(TAG,"driver deregistering failed");
}


void UartTerminal::init()
{
	if (m_uart == CONFIG_CONSOLE_UART_NUM) {
		// console is initialized seperately
		return;
	}
	if (m_uart >= UART_NUM_MAX) {
		m_error = "no such uart";
		return;
	}
#ifdef ESP32
	if (0 != uart_driver_install((uart_port_t)m_uart,UART_FIFO_LEN*8,0,0,0,0) ) {
#elif defined ESP8266
	if (0 != uart_driver_install((uart_port_t)m_uart,UART_FIFO_LEN*2,UART_FIFO_LEN*2,0,DRIVER_ARG) ) {
#else
#error missing implementation
#endif
		m_error = "driver install failed";
	}
}


int UartTerminal::read(char *buf, size_t s, bool block)
{
	int n = uart_read_bytes((uart_port_t)m_uart, (uint8_t*)buf, s, block ? portMAX_DELAY : 0);
	if (n < 0)
		m_error = esp_err_to_name(n);
	return n;
}


void UartTerminal::set_baudrate(unsigned br)
{
	if (m_uart == CONFIG_CONSOLE_UART_NUM) {
		log_error(TAG,"cannot change baudrate of console");
		return;
	}
	if (ESP_OK == uart_set_baudrate((uart_port_t)m_uart,br)) {
		log_info(TAG,"set baudrate for uart %u to %u",m_uart,br);
	} else {
		log_error(TAG,"failed to set baudrate for uart %u to %u",m_uart,br);
	}
}


int UartTerminal::write(const char *str, size_t l)
{
	int n = uart_write_bytes((uart_port_t)m_uart,str,l);
	if (n < 0) 
		m_error = esp_err_to_name(n);
	return n;
}


#ifdef CONFIG_TERMSERV
typedef enum { con_inp, con_esc, con_fin } con_st_t;


static void handle_input(con_st_t &state, char c)
{
	switch (state) {
	case con_inp:
		if (c == '~')
			state = con_esc;
		else
			uart_write_bytes((uart_port_t)CONFIG_TERMSERV_TX,&c,1);
		break;
	case con_esc:
		switch (c) {
		case '.':
			state = con_fin;
			break;
		case '~':
			uart_write_bytes((uart_port_t)CONFIG_TERMSERV_TX,&c,1);
			state = con_inp;
			break;
		default:
			state = con_inp;
		}
		break;
	case con_fin:
		break;
	default:
		abort();
	}
}


int uart_termcon(Terminal &term, int argc, const char *args[])
{
	//static atomic<uint8_t> Connections(0);	// still unsupported on ESP
	static uint8_t Connections = 0;
	if (argc != 1) {
		term.printf("%s: 0 arguments expected, got %u\n",args[0],argc-1);
		return 1;
	}
	if (++Connections > 1) {
		--Connections;
		term.printf("already connected\n");
		return 1;
	}
	term.printf("connecting to uart %d. Enter ~. to escape\n",CONFIG_TERMSERV_UART);
	con_st_t state = con_inp;
	while (1) {
		size_t a;
		if (esp_err_t e = uart_get_buffered_data_len((uart_port_t)CONFIG_TERMSERV_RX,&a)) {
			--Connections;
			term.printf("uart error %x\n",e);
			return 1;
		}
		if (a > 0) {
			//log_info(TAG,"%u bytes av",a);
			char buf[128];
			int r = uart_read_bytes((uart_port_t)CONFIG_TERMSERV_RX, (uint8_t*)buf, a < sizeof(buf) ? a : sizeof(buf), 0);
			if (r > 0) {
				//log_info(TAG,"%u bytes wr",a);
				term.write(buf,r);
				//uart_write_bytes((uart_port_t)0,buf,r);
			}
		}
		char in;
		int i = term.read(&in,1,false);
		if (i == 1) {
			handle_input(state,in);
			if (state == con_fin) {
				--Connections;
				return 0;
			}
		}
		if ((a == 0) && (i <= 0))
			vTaskDelay(100/portTICK_PERIOD_MS);
	}
	return 0;
}

#endif
