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

#ifndef CONFIG_CONSOLE_UART_NUM
#define CONFIG_CONSOLE_UART_NUM CONFIG_ESP_CONSOLE_UART_NUM
#endif

#include "log.h"
#include "uart_terminal.h"

#include <freertos/FreeRTOS.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#if defined CONFIG_IDF_TARGET_ESP32 && IDF_VERSION >= 40
#include <esp32/rom/gpio.h>
#else
#include <rom/gpio.h>
#endif

extern "C" {
#include <esp_task_wdt.h>
}

#include <stdarg.h>
#include <stdio.h>

#include <atomic>

using namespace std;


#define TAG MODULE_UART

#if IDF_VERSION > 32
#define DRIVER_ARG 0,0
#else
#define DRIVER_ARG 0
#endif

// ESP8266
// uart0/rx: gpio3/pin25
// uart0/tx: gpio1/pin26
// uart1/rx: gpio8/pin23	(not provided on d1 mini)
// uart1/tx: gpio2/pin14

// ESP32
// uart0/rx: gpio3/pin
// uart0/tx: gpio1/pin
// uart1/rx: gpio9/pin
// uart1/tx: gpio10/pin
// uart2/rx: gpio16/pin
// uart2/tx: gpio17/pin


UartTerminal::UartTerminal(bool crnl)
: Terminal(crnl)
, m_uart_rx(UART_NUM_MAX)
, m_uart_tx(UART_NUM_MAX)
{
	memcpy(m_name,"uart",5);
}


void UartTerminal::init(uint8_t uart)
{
	m_uart_rx = uart;
	m_uart_tx = uart;
#if CONFIG_UART_CONSOLE_NONE != 1 && CONFIG_CONSOLE_UART_NUM != -1
	if ((int)uart != CONFIG_CONSOLE_UART_NUM)
#endif
		uart_driver_install((uart_port_t)uart,UART_FIFO_LEN*2,UART_FIFO_LEN*2,0,DRIVER_ARG);
	snprintf(m_name,sizeof(m_name),"uart@%d",uart);
}


void UartTerminal::init(uint8_t rx, uint8_t tx)
{
	m_uart_rx = rx;
	m_uart_tx = tx;
	snprintf(m_name,sizeof(m_name),"uart@%d,%d",rx,tx);
#if CONFIG_UART_CONSOLE_NONE != 1 && CONFIG_CONSOLE_UART_NUM != -1
	if ((int)rx != CONFIG_CONSOLE_UART_NUM)
#endif
		uart_driver_install((uart_port_t)rx,UART_FIFO_LEN*2,UART_FIFO_LEN*2,0,DRIVER_ARG);
	if (rx != tx)
		uart_driver_install((uart_port_t)tx,UART_FIFO_LEN*2,UART_FIFO_LEN*2,0,DRIVER_ARG);
}


int UartTerminal::read(char *buf, size_t s, bool block)
{
	int n = uart_read_bytes((uart_port_t)m_uart_rx, (uint8_t*)buf, s, block ? portMAX_DELAY : 0);
	if (n < 0)
		m_error = esp_err_to_name(n);
	return n;
}


void UartTerminal::set_baudrate(unsigned br)
{
#if CONFIG_UART_CONSOLE_NONE != 1
	if (m_uart_rx == CONFIG_CONSOLE_UART_NUM) {
		log_error(TAG,"cannot change baudrate of console");
	} else 
#endif
	if (ESP_OK == uart_set_baudrate((uart_port_t)m_uart_rx,br)) {
		log_info(TAG,"uart %u baudrate %u",(unsigned)m_uart_rx,br);
	} else {
		log_error(TAG,"failed to set baudrate for uart %u to %u",(unsigned)m_uart_rx,br);
	}
	if (m_uart_rx == m_uart_tx)
		return;
#if CONFIG_UART_CONSOLE_NONE != 1
	if (m_uart_tx == CONFIG_CONSOLE_UART_NUM) {
		log_error(TAG,"cannot change baudrate of console");
	} else 
#endif
	if (ESP_OK == uart_set_baudrate((uart_port_t)m_uart_tx,br)) {
		log_info(TAG,"set baudrate for uart %u to %u",(unsigned)m_uart_tx,br);
	} else {
		log_error(TAG,"failed to set baudrate for uart %u to %u",(unsigned)m_uart_tx,br);
	}
}


int UartTerminal::write(const char *str, size_t l)
{
	int n = uart_write_bytes((uart_port_t)m_uart_tx,str,l);
	if (n < 0) 
		m_error = esp_err_to_name(n);
	return n;
}


void UartTerminal::sync(bool block)
{
	uart_wait_tx_done((uart_port_t)m_uart_tx,block ? portMAX_DELAY : 0);
}
