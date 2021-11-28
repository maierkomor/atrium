/*
 *  Copyright (C) 2021, Thomas Maier-Komor
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

#ifndef TCPIO_H
#define TCPIO_H

#include <stddef.h>
#include <freertos/FreeRTOS.h>

typedef struct tcpwrite_arg {
	struct tcp_pcb *pcb;
	size_t size;
	char *data;
	const char *name;
	xSemaphoreHandle sem;
} tcpwrite_arg_t;

#ifdef __cplusplus
extern "C" {
#endif

void tcpwrite_fn(void *arg);
void tcpwriteout_fn(void *arg);

#ifdef __cplusplus
}
#endif

#endif
