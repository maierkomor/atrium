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

#include "binformats.h"
#include "globals.h"
#include "log.h"
#include "shell.h"
#include "uarts.h"
#include "uart_terminal.h"
#include "versions.h"

#include "freertos/FreeRTOS.h"
#include <freertos/task.h>
#include <driver/uart.h>

#if IDF_VERSION > 32 || defined CONFIG_IDF_TARGET_ESP32
#define DRIVER_ARG 0,0
#else
#define DRIVER_ARG 0
#endif

#ifdef CONFIG_IDF_TARGET_ESP32
#include <soc/io_mux_reg.h>
#endif

// CONFIG_IDF_TARGET_ESP8266
// uart0/rx: gpio3/pin25
// uart0/tx: gpio1/pin26
// uart1/rx: gpio8/pin23	(not provided on d1 mini)
// uart1/tx: gpio2/pin14

// CONFIG_IDF_TARGET_ESP32
// uart0/rx: gpio3/pin
// uart0/tx: gpio1/pin
// uart1/rx: gpio9/pin
// uart1/tx: gpio10/pin
// uart2/rx: gpio16/pin
// uart2/tx: gpio17/pin

static const char TAG[] = "uart";

void diag_setup()
{
	int diag = HWConf.system().diag_uart();
	if (diag != -1) {
		if (esp_err_t e = uart_driver_install((uart_port_t)diag,256,256,0,DRIVER_ARG))
			log_error(TAG,"uart %d driver error: %s",diag,esp_err_to_name(e));
		log_set_uart(diag);
	}
#ifdef CONFIG_IDF_TARGET_ESP32
	for (const UartConfig &c : HWConf.uart()) {
		if (c.has_port()) {
			int8_t p = c.port();
			int8_t tx = c.has_tx_gpio() ? c.tx_gpio() : UART_PIN_NO_CHANGE;
			int8_t rx = c.has_rx_gpio() ? c.rx_gpio() : UART_PIN_NO_CHANGE;
			int8_t cts = c.has_cts_gpio() ? c.cts_gpio() : UART_PIN_NO_CHANGE;
			int8_t rts = c.has_rts_gpio() ? c.rts_gpio() : UART_PIN_NO_CHANGE;
			if (esp_err_t e = uart_set_pin((uart_port_t)p, tx, rx, cts, rts))
				log_error(TAG,"set pin failed: %s",esp_err_to_name(e));
			else
				log_info(TAG,"uart %d pins set to tx=%d,rx=%d,cts=%d,rts=%d",p,tx,rx,cts,rts);
			switch (p) {
				/*
			case 1:
				if (rx == -1) {
					PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA2_U,FUNC_SD_DATA2_U1RXD);
				}
				if (tx == -1) {
					PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA3_U,FUNC_SD_DATA3_U1TXD);
				}
				break;
				*/
			case 2:
				if (rx == -1) {
					PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO16_U,FUNC_GPIO16_U2RXD);
				}
				if (tx == -1) {
					PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO17_U,FUNC_GPIO17_U2TXD);
				}
				break;
			default:
				;
			}
		}
	}
#endif
}


void uart_setup()
{
	log_info(TAG,"uart setup");
	for (const UartSettings &c : Config.uart()) {
		if (!c.has_port()) {
			log_warn(TAG,"ignoring config where port is not set");
			continue;
		}
		uart_port_t port = (uart_port_t) c.port();
#if 0 //CONFIG_UART_CONSOLE_NONE != 1
		if (port == CONFIG_CONSOLE_UART_NUM) {
			log_info(TAG,"console uart already configured");
			continue;
		}
#endif
		log_info(TAG,"configuring port %d",port);
		uart_config_t uc;
		bzero(&uc,sizeof(uc));
		uc.baud_rate = c.has_baudrate() ? c.baudrate() : 115200;
		if (c.has_config()) {
			uint16_t config = c.config();
			uc.data_bits = (uart_word_length_t) (config & 3);
			uc.stop_bits = (uart_stop_bits_t) ((config >> 2) & 3);
			uc.parity = (uart_parity_t) ((config >> 6) & 0xf);
			uc.flow_ctrl = (uart_hw_flowcontrol_t) ((config >> 4) & 3);
#ifdef CONFIG_IDF_TARGET_ESP32
			uc.use_ref_tick = (config >> 10) & 1;
#endif
		} else {
			// default: 8N1
			uc.data_bits = UART_DATA_8_BITS;
			uc.parity = UART_PARITY_DISABLE;
			uc.stop_bits = UART_STOP_BITS_1;
			uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
#ifdef CONFIG_IDF_TARGET_ESP32
			uc.use_ref_tick = false;
#endif
		}
		unsigned rx_buf = c.has_rx_bufsize() ? c.rx_bufsize() : 256;
		unsigned tx_buf = c.has_tx_bufsize() ? c.tx_bufsize() : 256;
		//uart_driver_delete(port);
/*
#ifdef CONFIG_IDF_TARGET_ESP32
		switch (port) {
		case 0:
			//if (esp_err_t e = uart_set_pin(UART_NUM_0,GPIO_NUM_4,GPIO_NUM_5,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE))
			if (esp_err_t e = uart_set_pin(UART_NUM_0,GPIO_NUM_1,GPIO_NUM_22,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE))
				log_error(TAG,"set pin failure: %d",e);
			else
				log_info(TAG,"set pin OK");
			break;
		case 1:
			//PIN_FUNC_SELECT(FUNC_GPIO16_U2RXD
			if (esp_err_t e = uart_set_pin(UART_NUM_1,GPIO_NUM_19,GPIO_NUM_18,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE))
				log_error(TAG,"set pin failure: %d",e);
			else
				log_info(TAG,"set pin OK");
			break;
		case 2:
			//PIN_FUNC_SELECT(FUNC_GPIO16_U2RXD
			//if (esp_err_t e = uart_set_pin(UART_NUM_2,GPIO_NUM_17,GPIO_NUM_16,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE))
			if (esp_err_t e = uart_set_pin(UART_NUM_2,UART_PIN_NO_CHANGE,GPIO_NUM_4,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE))
				log_error(TAG,"set pin failure: %d",e);
			else
				log_info(TAG,"set pin OK");
			break;
		default:
			;
		}
#endif
*/
		if (port == HWConf.system().diag_uart()) { 
			// already handled
			log_info(TAG,"port %d is diag port, no driver needed",port);
		} else if (esp_err_t e = uart_param_config(port,&uc)) {
			log_error(TAG,"unable to configure uart %d: %s",port,esp_err_to_name(e));
		} else if (esp_err_t e = uart_driver_install(port,rx_buf,tx_buf,0,DRIVER_ARG)) {
			log_error(TAG,"unable to install uart driver on port %d: %s",port,esp_err_to_name(e));
		} else {
			log_info(TAG,"uart%u setup done",port);
		}
	}
}



#ifdef CONFIG_TERMSERV
typedef enum { con_inp, con_esc, con_fin } con_st_t;


static void handle_input(con_st_t &state, char c, uart_port_t tx)
{
	switch (state) {
	case con_inp:
		if (c == '~')
			state = con_esc;
		else if ((int)tx >= 0)
			uart_write_bytes(tx,&c,1);
		break;
	case con_esc:
		switch (c) {
		case '.':
			state = con_fin;
			break;
		case '~':
			if ((int)tx >= 0)
				uart_write_bytes(tx,&c,1);
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
	static volatile uint8_t Connections = 0;
	int8_t con;
	if (argc > 2)
		return arg_invnum(term);
	if (argc == 2) {
		con = -1;
		if ((args[1][0] >= '0') && (args[1][0] <= '9') && (args[1][1] == 0)) {
			con = args[1][0] - '0';
		} else {
			int8_t c = 0;
			for (auto &t : Config.terminal()) {
				if (0 == strcmp(args[1],t.name().c_str())) {
					con = c;
					break;
				}
				++c;
			}
		}
		if (con == -1)
			return arg_invalid(term,args[1]);
	} else {
		con = 0;
	}
	if (con >= Config.terminal_size())
		return arg_invalid(term,args[1]);
	const TerminalConfig &t = Config.terminal(con);
	uart_port_t rx = (uart_port_t) t.uart_rx();
	uart_port_t tx = (uart_port_t) t.uart_tx();
	Connections = Connections ^ (1 << con);
	if ((Connections & (1 << con)) == 0) {
		Connections = Connections ^ (1 << con);
		term.println("already connected");
		return 1;
	}
	if ((rx == -1) && (tx == -1)) {
		term.println("no uarts configured");
		return 1;
	}
	term.printf("connecting to uart rx%d/tx%d. Enter ~. to escape\n",(int)rx,(int)tx);
	con_st_t state = con_inp;
#define CON_BUF_SIZE 196
	uint8_t *buf = (uint8_t *) malloc(CON_BUF_SIZE);
	if (buf == 0) {
		term.printf("out of memory");
		return 1;
	}
	bool crnl = term.get_crnl();
	term.set_crnl(false);
	while (1) {
		size_t a = 0;
		if (rx >= 0) {
			if (esp_err_t e = uart_get_buffered_data_len(rx,&a)) {
				Connections = Connections ^ (1 << con);
				term.printf("uart error: %s\n",esp_err_to_name(e));
				term.set_crnl(crnl);
				free(buf);
				return 1;
			}
			if (a > 0) {
				//log_info(TAG,"%u bytes av",a);
				int r = uart_read_bytes(rx, buf, a < CON_BUF_SIZE ? a : CON_BUF_SIZE, 0);
				if (r > 0) {
					//log_info(TAG,"%u bytes wr",a);
					term.write((const char*)buf,r);
				}
			}
		}
		char in;
		int i = term.read(&in,1,false);
		if (i == 1) {
			handle_input(state,in,tx);
			if (state == con_fin)
				break;
		} else if ((i == -1) && term.error()) {
			Connections = Connections ^ (1 << con);
			const char *e = term.error();
			log_info(TAG,"terminal read error: %s; closing console session", e ? e : "<null>");
			free(buf);
			return 1;
		}
		if ((a == 0) && (i <= 0))
			vTaskDelay(100/portTICK_PERIOD_MS);
	}
	free(buf);
	Connections = Connections ^ (1 << con);
	term.set_crnl(crnl);
	return 0;
}

#endif // CONFIG_TERMSERV
