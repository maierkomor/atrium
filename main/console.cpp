/*
 *  Copyright (C) 2017-2023, Thomas Maier-Komor
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

#include <esp_err.h>
#include <string.h>

#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "shell.h"
#include "swcfg.h"
#include "terminal.h"

using namespace std;


#define TAG MODULE_CON

#ifndef CONFIG_CONSOLE_STACK_SIZE
#define CONFIG_CONSOLE_STACK_SIZE 4096
#endif

#ifndef PRO_CPU_NUM
#define PRO_CPU_NUM 0
#endif

void console_task(void *con)
{
	Terminal *term = (Terminal *)con;
	log_info(TAG,"started console on %s",term->type());
	for (;;) {
		shell(*term);
	}
}


#ifdef CONFIG_UART_CONSOLE
#include "uart_terminal.h"

static inline void uart_console_setup(void)
{
	int rx = HWConf.system().console_rx();
	int tx = HWConf.system().console_tx();
	if ((tx != -1) && (rx != -1)) {
		UartTerminal *con = new UartTerminal(true);
		con->init(rx,tx);
		if (!Config.has_pass_hash())
			con->setPrivLevel(1);
		BaseType_t r = xTaskCreatePinnedToCore(console_task, "ttyS", CONFIG_CONSOLE_STACK_SIZE, con, 8, 0, PRO_CPU_NUM);
		if (r != pdPASS)
			log_warn(TAG,"failed to create task for uart ttyS: %d",r);
	}
}
#else
#define uart_console_setup()
#endif // CONFIG_UART_CONSOLE


#if defined CONFIG_USB_CONSOLE
#include "jtag_terminal.h"

static inline void jtag_console_setup(void)
{
	if (HWConf.system().usb_con()) {
		if (JtagTerminal *con = new JtagTerminal(true)) {
			if (!Config.has_pass_hash())
				con->setPrivLevel(1);
			BaseType_t r = xTaskCreatePinnedToCore(console_task, "ttyJ", CONFIG_CONSOLE_STACK_SIZE, con, 8, 0, PRO_CPU_NUM);
			if (r != pdPASS)
				log_warn(TAG,"failed to create task for uart ttyJ: %d",r);
		}
	}
}
#else
#define jtag_console_setup()
#endif // CONFIG_USB_CONSOLE


#if defined CONFIG_USB_CONSOLE && defined CONFIG_TINYUSB_CDC_ENABLED
#include "cdc_terminal.h"

static inline void cdc_console_setup(void)
{
	if (CdcTerminal *con = CdcTerminal::create(true)) {
		if (!Config.has_pass_hash())
			con->setPrivLevel(1);
		BaseType_t r = xTaskCreatePinnedToCore(console_task, "ttyU", CONFIG_CONSOLE_STACK_SIZE, con, 8, 0, PRO_CPU_NUM);
		if (r != pdPASS)
			log_warn(TAG,"failed to create task for uart ttyU: %d",r);
	}
}
#else
#define cdc_console_setup()
#endif


#ifdef CONFIG_USB_DIAGLOG
extern "C" {
	extern uint8_t UsbDiag;
}
#endif

void console_setup()
{
#ifdef CONFIG_USB_DIAGLOG
	UsbDiag = HWConf.system().usb_diag();
#endif
	uart_console_setup();
	jtag_console_setup();
	cdc_console_setup();
}
