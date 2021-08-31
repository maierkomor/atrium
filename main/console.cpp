/*
 *  Copyright (C) 2017-2021, Thomas Maier-Komor
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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <driver/uart.h>

#include "log.h"
#include <esp_err.h>

#include <string.h>

#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "shell.h"
#include "swcfg.h"
#include "uart_terminal.h"

#if IDF_VERSION > 32 || defined CONFIG_IDF_TARGET_ESP32
#define DRIVER_ARG 0,0
#else
#define DRIVER_ARG 0
#endif

using namespace std;

static const char TAG[] = "con";


#ifdef CONFIG_UART_CONSOLE

static void console_task(void *con)
{
	log_dbug(TAG,"ready");
	UartTerminal *term = (UartTerminal *)con;
	for (;;) {
		shell(*term);
	}
}


int console_setup(void)
{
	int rx = HWConf.system().console_rx();
	int tx = HWConf.system().console_tx();
	if ((tx != -1) && (rx != -1)) {
		UartTerminal *con = new UartTerminal;
		con->init(rx,tx);
		if (!Config.has_pass_hash())
			con->setPrivLevel(1);
		BaseType_t r = xTaskCreatePinnedToCore(console_task, "tty", 4096, con, 8, 0, PRO_CPU_NUM);
		if (r != pdPASS) {
			log_error(TAG,"create task: %s",esp_err_to_name(r));
			return 1;
		}
	}
	return 0;
}

#endif // CONFIG_UART_CONSOLE


extern "C"
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


