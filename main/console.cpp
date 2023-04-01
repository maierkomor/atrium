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

#include "log.h"
#include <esp_err.h>

#include <string.h>

#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "shell.h"
#include "swcfg.h"

using namespace std;

#define TAG MODULE_CON


static void console_task(void *con)
{
	Terminal *term = (Terminal *)con;
	for (;;) {
		shell(*term);
	}
}


#ifdef CONFIG_UART_CONSOLE
#include "uart_terminal.h"

#if IDF_VERSION > 32 || defined CONFIG_IDF_TARGET_ESP32
#define DRIVER_ARG 0,0
#else
#define DRIVER_ARG 0
#endif

static inline void uart_console_setup(void)
{
	int rx = HWConf.system().console_rx();
	int tx = HWConf.system().console_tx();
	if ((tx != -1) && (rx != -1)) {
		UartTerminal *con = new UartTerminal(true);
		con->init(rx,tx);
		if (!Config.has_pass_hash())
			con->setPrivLevel(1);
		BaseType_t r = xTaskCreatePinnedToCore(console_task, "tty", 4096, con, 8, 0, PRO_CPU_NUM);
		if (r != pdPASS)
			log_error(TAG,"create task: %s",esp_err_to_name(r));
		else
			log_info(TAG,"console on UART %d/%d",rx,tx);
	}
}
#else
#define uart_console_setup()
#endif // CONFIG_UART_CONSOLE


#if defined CONFIG_USB_CONSOLE && defined CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "jtag_terminal.h"

static inline void jtag_console_setup(void)
{
	if (HWConf.system().usb_con()) {
		if (JtagTerminal *con = new JtagTerminal(true)) {
			if (!Config.has_pass_hash())
				con->setPrivLevel(1);
			BaseType_t r = xTaskCreatePinnedToCore(console_task, "jtagtty", 4096, con, 8, 0, PRO_CPU_NUM);
			if (r != pdPASS)
				log_error(TAG,"create task: %s",esp_err_to_name(r));
			else
				log_info(TAG,"console on JTAG");
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
		BaseType_t r = xTaskCreatePinnedToCore(console_task, "cdctty", 4096, con, 8, 0, PRO_CPU_NUM);
		if (r != pdPASS)
			log_error(TAG,"create task: %s",esp_err_to_name(r));
		else
			log_info(TAG,"console on CDC");
	}
}
#else
#define cdc_console_setup()
#endif


void console_setup()
{
	uart_console_setup();
	jtag_console_setup();
	cdc_console_setup();
}
