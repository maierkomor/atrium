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

#ifdef CONFIG_DMESG
#define DMESG_DEFAULT_SIZE 2048

#include "log.h"
#include "terminal.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/uart.h>


static SemaphoreHandle_t Lock;
static char *Buf = 0;
static uint16_t At,Size;


extern "C"
void dmesg_log(const char *m, size_t ml)
{
	if (Buf == 0)
		return;
	if (ml > (Size>>2))
		return;
	xSemaphoreTake(Lock,portMAX_DELAY);
	while (At+ml >= Size) {
		char *nl = (char*)memchr(Buf,'\n',At);
		if (nl == 0) {
			At = 0;
			break;
		}
		++nl;
		memmove(Buf,nl,Buf+At-nl);
		At -= (nl-Buf);
	}
	memcpy(Buf+At,m,ml);
	At += ml;
	xSemaphoreGive(Lock);
}


void dmesg_to_uart(int8_t uart)
{
	xSemaphoreTake(Lock,portMAX_DELAY);
	uart_write_bytes((uart_port_t)uart,Buf,At);
	xSemaphoreGive(Lock);
}


int dmesg(Terminal &term, int argc, const char *args[])
{
#ifdef write
#undef write
#endif
	if (argc > 2) {
		term.printf("invalid number of arguments\n");
		return 1;
	}
	if (argc == 2) {
		char *eptr;
		long l = strtol(args[1],&eptr,0);
		if (eptr == args[1]) {
			term.printf("invalid argument\n");
			return 1;
		}
		return dmesg_resize(l);
	}
	if (Buf == 0) {
		term.printf("dmesg is inactive\n");
		return 1;
	}
	xSemaphoreTake(Lock,portMAX_DELAY);
	term.write(Buf,At);
	xSemaphoreGive(Lock);
	return 0;
}


extern "C"
void dmesg_setup()
{
	Lock = xSemaphoreCreateMutex();
	Buf = (char *) malloc(DMESG_DEFAULT_SIZE);
	At = 0;
	Size = DMESG_DEFAULT_SIZE;
}

extern "C"
int dmesg_resize(unsigned s)
{
	if (s > UINT16_MAX)
		return -1;
	if (s == Size)
		return 0;
	int ret;
	xSemaphoreTake(Lock,portMAX_DELAY);
	if (s == 0) {
		if (Buf)
			free(Buf);
		Buf = 0;
		ret = 0;
	} else {
		char *n = (char *) realloc(Buf,s);
		if (n != 0) {
			if (At > s) {
				// shrinking dmesg may clear it!
				At = 0;
			}
			Buf = n;
			Size = s;
			ret = 0;
		} else {
			// resize failed, so keep the current buffer
			ret = -2;
		}
	}
	xSemaphoreGive(Lock);
	return ret;
}

#endif
