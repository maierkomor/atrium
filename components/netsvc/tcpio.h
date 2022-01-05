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
#include <lwip/err.h>


typedef struct tcpout_arg {
	struct tcp_pcb *pcb;
	const char *name;
	err_t err;
	xSemaphoreHandle sem;
} tcpout_arg_t;

typedef struct tcpwrite_arg {
	struct tcp_pcb *pcb;
	const char *name;
	const char *data;
	size_t size;
	err_t err;
	xSemaphoreHandle sem;
} tcpwrite_arg_t;

typedef struct tcp_pbuf_arg {
	struct pbuf *pbuf;
	xSemaphoreHandle sem;
} tcp_pbuf_arg_t;

#ifdef __cplusplus
extern "C" {
#endif

void tcpout_fn(void *arg);
void tcpwrite_fn(void *arg);
void tcpwriteout_fn(void *arg);
void tcp_pbuf_free_fn(void *);

#ifdef __cplusplus
}
#endif

#endif
