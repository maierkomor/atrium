/*
 *  Copyright (C) 2017-2020, Thomas Maier-Komor
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


#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <driver/uart.h>

#include "log.h"
#include <esp_err.h>

#include <string.h>

#include <sdkconfig.h>
#include "globals.h"
#include "log.h"
#include "shell.h"
#include "uart_terminal.h"

using namespace std;

#ifdef ESP8266
#if CONFIG_TERMSERV == 0
#define ENABLE_CONSOLE
#endif
#elif CONFIG_CONSOLE_UART_NONE == 0
#define ENABLE_CONSOLE
#endif


#ifdef ENABLE_CONSOLE


extern SemaphoreHandle_t UartLock;
static char TAG[] = "con";

static UartTerminal Console(CONFIG_CONSOLE_UART_NUM);


static void console_task(void *ignored)
{
	for (;;) {
		char buf[64];
		int n = Console.readInput(buf,sizeof(buf)-1,true);
		if (n > 0) {
			buf[n] = 0;
			shellexe(Console,buf);
		} else {
			//log_info(TAG,"readInput() = %d",n);
		}
	}
}


extern "C"
void console_setup()
{
	Console.init();
	BaseType_t r = xTaskCreatePinnedToCore(console_task, "tty", 10000, NULL, 15, 0, PRO_CPU_NUM);
	if (r != pdPASS)
		log_error(TAG,"task creation failed: %s",esp_err_to_name(r));
	else
		log_info(TAG,"started console");
}

#else

extern "C"
void console_setup()
{
}

#endif // CONFIG_CONSOLE_UART_NONE
