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


#include <sdkconfig.h>

#ifndef CONFIG_IDF_TARGET_ESP8266
#include "log.h"
#include "tcpio.h"

#include <freertos/semphr.h>

#include <lwip/inet.h>
#include <lwip/tcpip.h>
#include <lwip/tcp.h>

#define TAG MODULE_LWTCP

void tcpwrite_fn(void *arg)
{
	tcpwrite_arg_t *r = (tcpwrite_arg_t *)arg;
	r->err = tcp_write(r->pcb,r->data,r->size,TCP_WRITE_FLAG_COPY|TCP_WRITE_FLAG_MORE);
	if (r->err) {
		log_warn(TAG,"%s write %d",r->name,r->err);
	}
	xSemaphoreGive(r->sem);
}


void tcpwriteout_fn(void *arg)
{
	tcpwrite_arg_t *r = (tcpwrite_arg_t *)arg;
	r->err = tcp_write(r->pcb,r->data,r->size,TCP_WRITE_FLAG_COPY);
	if (r->err)
		log_warn(TAG,"%s write %d",r->name,r->err);
	r->err = tcp_output(r->pcb);
	if (r->err)
		log_warn(TAG,"%s output %d",r->name,r->err);
	xSemaphoreGive(r->sem);
}


void tcpout_fn(void *arg)
{
	tcpout_arg_t *r = (tcpout_arg_t *)arg;
	r->err = tcp_output(r->pcb);
	if (r->err)
		log_warn(TAG,"%s output %d",r->name,r->err);
	xSemaphoreGive(r->sem);
}


void tcp_pbuf_free_fn(void *arg)
{
	tcp_pbuf_arg_t *a = (tcp_pbuf_arg_t *) arg;
	pbuf_free(a->pbuf);
	xSemaphoreGive(a->sem);
}
#endif
