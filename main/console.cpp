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

#ifdef CONFIG_UART_CONSOLE

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <driver/uart.h>

#include "log.h"
#include <esp_err.h>

#include <string.h>

#include "binformats.h"
#include "globals.h"
#include "log.h"
#include "shell.h"
#include "uart_terminal.h"

using namespace std;

#ifndef CONFIG_CONSOLE_UART_NUM
#define CONFIG_CONSOLE_UART_NUM CONFIG_ESP_CONSOLE_UART_NUM
#endif

extern SemaphoreHandle_t UartLock;

static const char TAG[] = "con";
static UartTerminal Console(CONFIG_CONSOLE_UART_NUM);

int ps(Terminal &term, int argc, const char *args[]);

static void console_task(void *ignored)
{
	for (;;) {
		shell(Console);
	}
}


int console_setup(void)
{
	int rx = HWConf.system().console_rx();
	int tx = HWConf.system().console_tx();
	if ((rx == -1) || (tx == -1))
		return 0;
	Console.init(rx,tx);
	if (!Config.has_pass_hash())
		Console.setPrivLevel(1);
	BaseType_t r = xTaskCreatePinnedToCore(console_task, "tty", 4096, NULL, 19, 0, 0); //PRO_CPU_NUM);
	if (r != pdPASS) {
		log_error(TAG,"task creation failed: %s",esp_err_to_name(r));
		return 1;
	}
	return 0;
}

#endif // CONFIG_CONSOLE_UART_NONE
