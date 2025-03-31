/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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
#include "hwcfg.h"
#include "log.h"
#include "swcfg.h"
#include "uarts.h"
#include "uart_terminal.h"

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

#if CONFIG_UART_CONSOLE_NONE != 1
#ifndef CONFIG_CONSOLE_UART_NUM
#define CONFIG_CONSOLE_UART_NUM 0
#endif
#endif

#ifndef UART_HW_FIFO_LEN
#define UART_HW_FIFO_LEN(x) UART_FIFO_LEN
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

struct Term
{
	Term(int8_t r, int8_t t)
	: rx(r), tx(t)
	{ }

	char *name = 0;
	Term *next = 0;
	uint8_t rx,tx;
	bool connected = false;
};


#define TAG MODULE_UART
#ifdef CONFIG_TERMSERV
static Term *Terminals = 0;
#endif


#ifdef CONFIG_UART_MONITOR
static volatile uint8_t Monitors = 0;

static void monitor(void *uart)
{
	int rx_uart = (int) uart;
	char buf[256];
	size_t fill = 0;
	for (;;) {
		char *in = buf + fill;
		int n = uart_read_bytes((uart_port_t)rx_uart, (uint8_t*)in, sizeof(buf)-1-fill, portMAX_DELAY);
		if (n < 0) {
			log_error(TAG,"read error %d",n);
			vTaskDelete(0);
		}
		fill += n;
		buf[fill] = 0;
		char *at = buf, *nl = strchr(in,'\n');
		while (nl) {
			if (nl-at > 0)
				log_syslog(ll_info,MODULE_UART,at,nl-at,0);
			at = nl+1;
			if (*at == '\r')
				++at;
			nl = strchr(at,'\n');
		}
		if (at > buf) {
			fill -= at-buf;
			if (fill)
				memmove(buf,at,fill);
		} else if (fill == sizeof(buf)-1) {
			assert(buf == at);
			log_syslog(ll_info,MODULE_UART,at,nl-at,0);
			in = buf;
			fill = 0;
		}
	}
}
#endif //CONFIG_MONITOR


void uart_setup()
{
	for (const UartSettings &c : Config.uart()) {
		if (!c.has_port()) {
			log_warn(TAG,"incomplete config");
			continue;
		}
		uart_port_t port = (uart_port_t) c.port();
		uart_config_t uc;
		bzero(&uc,sizeof(uc));
		uc.baud_rate = c.has_baudrate() ? c.baudrate() : 115200;
		if (c.has_config()) {
			uc.data_bits = (uart_word_length_t) c.config_wl();
			uc.stop_bits = (uart_stop_bits_t) c.config_sb();
			if (uc.stop_bits == 0) {
				log_warn(TAG,"uart%u: stop bits is unset",port);
				uc.stop_bits = UART_STOP_BITS_1;
			}
			uc.parity = (uart_parity_t) c.config_p();
			uc.flow_ctrl = (uart_hw_flowcontrol_t) ((c.config_cts()<<1)|c.config_rts());
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2
#if IDF_VERSION >= 44
			uc.source_clk = c.config_ref_tick() ? UART_SCLK_REF_TICK : UART_SCLK_APB;
#else
			uc.use_ref_tick = c.config_ref_tick();
#endif
#endif
		} else {
			// default: 8N1
			uc.data_bits = UART_DATA_8_BITS;
			uc.parity = UART_PARITY_DISABLE;
			uc.stop_bits = UART_STOP_BITS_1;
			uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2
#if IDF_VERSION >= 44
			uc.source_clk = UART_SCLK_APB;
#else
			uc.use_ref_tick = false;
#endif
#endif
		}
		static const char *stop_bits[] = {"","1","1.5","2"};
		log_info(TAG,"uart%u: %u baud, %u%c%s",port,uc.baud_rate,uc.data_bits+5,uc.parity==0?'N':uc.parity==2?'E':'O',stop_bits[uc.stop_bits]);
		if (port == (uart_port_t) CONFIG_CONSOLE_UART_NUM) {
			// already handled
			log_info(TAG,"uart%d is console",port);
		} else if (esp_err_t e = uart_param_config(port,&uc)) {
			log_error(TAG,"uart%d config: %s",port,esp_err_to_name(e));
#ifndef CONFIG_IDF_TARGET_ESP32
		} else {
			unsigned rx_buf = c.has_rx_bufsize() ? c.rx_bufsize() : 256;
			if (rx_buf <= 128)
				log_warn(TAG,"rx buf size must be >=128");
			unsigned tx_buf = c.has_tx_bufsize() ? c.tx_bufsize() : (256+128);
			if (esp_err_t e = uart_driver_install(port,rx_buf,tx_buf,0,DRIVER_ARG))
				log_error(TAG,"uart%d driver: %s",port,esp_err_to_name(e));
#endif
		}
	}
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2
	// pin setting must be done after param-config and befor driver install!
	uint8_t uarts = 0;
	for (const auto &c : HWConf.uart()) {
		int8_t p = c.port();
		if (p == -1)
			continue;
		int8_t tx = c.tx_gpio();
		if (tx == -1)
			tx = UART_PIN_NO_CHANGE;
		int8_t rx = c.rx_gpio();
		if (rx == -1)
			rx = UART_PIN_NO_CHANGE;
		int8_t cts = c.cts_gpio();
		if (cts == -1)
			cts = UART_PIN_NO_CHANGE;
		int8_t rts = c.rts_gpio();
		if (rts == -1)
			rts = UART_PIN_NO_CHANGE;
		if (p == CONFIG_CONSOLE_UART_NUM)
			uart_driver_delete((uart_port_t)CONFIG_CONSOLE_UART_NUM);
		if (esp_err_t e = uart_set_pin((uart_port_t)p,tx,rx,rts,cts))
			log_warn(TAG,"set uart%d to tx=%d,rx=%d,rtx=%d,cts=%d: %s",p,tx,rx,rts,cts,esp_err_to_name(e));
		else
			log_info(TAG,"uart%d at tx=%d,rx=%d,rtx=%d,cts=%d",p,tx,rx,rts,cts);
		if (esp_err_t e = uart_driver_install((uart_port_t)p,UART_HW_FIFO_LEN(p)*2,UART_HW_FIFO_LEN(p)*2,0,DRIVER_ARG))
			log_warn(TAG,"uart%d driver: %s",p,esp_err_to_name(e));
		uarts |= 1<<p;
	}
	for (const UartSettings &c : Config.uart()) {
		uart_port_t port = (uart_port_t) c.port();
		unsigned rx_buf = c.has_rx_bufsize() ? c.rx_bufsize() : UART_HW_FIFO_LEN(port)*2;
		if (rx_buf <= 128)
			log_warn(TAG,"rx buf size must be >=128");
		unsigned tx_buf = c.has_tx_bufsize() ? c.tx_bufsize() : UART_HW_FIFO_LEN(port)*2;
		if (uarts & (1<<port))
			uart_driver_delete((uart_port_t)port);
		if (esp_err_t e = uart_driver_install(port,rx_buf,tx_buf,0,DRIVER_ARG))
			log_warn(TAG,"uart%d driver: %s",port,esp_err_to_name(e));
	}
#endif
	if (HWConf.system().has_diag_uart()) {
		int8_t diag = HWConf.system().diag_uart();
		if (diag == -1) {
			log_info(TAG,"diag on uart disabled");
		} else {
			log_set_uart(diag);
			log_info(TAG,"diag on uart %u",diag);
		}
	}
#ifdef CONFIG_TERMSERV
	uint8_t rx_used = 0, tx_used = 0;
	int crx = HWConf.system().console_rx();
	int ctx = HWConf.system().console_tx();
	if (crx != -1)
		rx_used = 1<<crx;
	if (ctx != -1)
		tx_used = 1<<ctx;
	for (auto &c : Config.terminal()) {
		int8_t rx = c.uart_rx();
		int8_t tx = c.uart_tx();
		if ((tx != -1) && (tx_used & (1 << tx))) {
			log_warn(TAG,"multiple use of uart%d/tx",tx);
			continue;
		}
		if ((rx != -1) && (rx_used & (1 << rx))) {
			log_warn(TAG,"multiple use of uart%d/rx",rx);
			continue;
		}
#ifdef CONFIG_UART_MONITOR
		if ((tx == -1) && (rx != -1)) {
			// this is a monitor
			BaseType_t r = xTaskCreatePinnedToCore(monitor, "mon", 2048, (void*)(int)rx, 19, 0, PRO_CPU_NUM);
			if (r == pdPASS) {
				rx_used |= (1 << rx);
				log_info(TAG,"monitor uart%d",rx);
			} else {
				log_error(TAG,"monitor task: %s",esp_err_to_name(r));
			}
		}
#endif
		if ((tx != -1) && (rx != -1)) {
			// this is a terminal
			Term *n = new Term(rx,tx);
			if (c.has_name()) {
				n->name = strdup(c.name().c_str());
			} else {
				asprintf(&n->name,"uart@%u,%u",rx,tx);
			}
			log_info(TAG,"terminal '%s' on %d/%d",n->name,rx,tx);
			if (Terminals == 0) {
				Terminals = n;
			} else {
				Term *l = Terminals;
				while (l->next)
					l = l->next;
				l->next = n;
			}
			rx_used |= (1 << rx);
			tx_used |= (1 << tx);
		}
	}
#endif
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


const char *uart_termcon(Terminal &term, int argc, const char *args[])
{
	if (argc != 2)
		return "Invalid number of arguments.";
	if (0 == strcmp("-l",args[1])) {
		Term *t = Terminals;
		while (t) {
			term.printf("rx=%d, tx=%d: %s\n",t->rx,t->tx,t->name);
			t = t->next;
		}
		return 0;
	}
	Term *t = Terminals;
	if ((args[1][0] >= '0') && (args[1][0] <= '9') && (args[1][1] == 0)) {
		int con = args[1][0] - '0';
		while (con) {
			if (t == 0)
				return "Invalid argument #1.";
			t = t->next;
			--con;
		}
	} else {
		while (t && strcmp(t->name,args[1]))
			t = t->next;
	}
	if (t == 0)
		return "No such terminal.";
	if (t->connected)
		return "Already connected.";
	t->connected = true;
	uart_port_t rx = (uart_port_t) t->rx;
	uart_port_t tx = (uart_port_t) t->tx;
	term.printf("connecting to uart rx%d/tx%d. Enter ~. to escape\n",(int)rx,(int)tx);
	con_st_t state = con_inp;
#define CON_BUF_SIZE 196
	uint8_t *buf = (uint8_t *) malloc(CON_BUF_SIZE);
	if (buf == 0) {
		t->connected = false;
		return "Out of memory.";
	}
	bool crnl = term.get_crnl();
	term.set_crnl(false);
	const char *r = 0;
	while (1) {
		size_t a = 0;
		if (rx >= 0) {
			if (esp_err_t e = uart_get_buffered_data_len(rx,&a)) {
				term.printf("uart error: %s\n",esp_err_to_name(e));
				r = "";
				break;
			}
			log_dbug(TAG,"recv %u",a);
			if (a > 0) {
				//log_dbug(TAG,"%u bytes av",a);
				int n = uart_read_bytes(rx, buf, a < CON_BUF_SIZE ? a : CON_BUF_SIZE, 0);
				if (n > 0) {
					//log_dbug(TAG,"%u bytes wr",a);
					term.write((const char*)buf,n);
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
			r = "Terminal error.";
			const char *e = term.error();
			log_info(TAG,"read error: %s", e ? e : "<null>");
			break;
		}
		if ((a == 0) && (i <= 0))
			vTaskDelay(100/portTICK_PERIOD_MS);
	}
	free(buf);
	t->connected = false;
	term.set_crnl(crnl);
	return r;
}


#endif // CONFIG_TERMSERV
