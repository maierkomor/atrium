/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#ifndef NETSVC_H
#define NETSVC_H

#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <lwip/ip_addr.h>
#include <lwip/tcpip.h>
#include <lwip/opt.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NETSVC_IMPL
extern char *Hostname, *Domainname;
extern uint8_t HostnameLen, DomainnameLen;
#else
extern const char *Hostname, *Domainname;
extern const uint8_t HostnameLen, DomainnameLen;
#endif

#if LWIP_IPV6
extern ip6_addr_t IP6G,IP6LL;
#endif

char *streol(char *b, size_t n);
int resolve_fqhn(const char *h, ip_addr_t *);
int resolve_hostname(const char *h, ip_addr_t *);
int query_host(const char *h, ip_addr_t *a, void (*cb)(const char *hn, const ip_addr_t *addr, void *arg), void *arg);
const char *strneterr(int socket);
extern SemaphoreHandle_t LwipMtx;
int sethostname(const char *h, size_t l);
int setdomainname(const char *h, size_t l);
const char *strlwiperr(int);

//const char *ip2str(const ip_addr_t *ip);
const char *ip2str_r(const ip_addr_t *ip, char *out, size_t n);

#if LWIP_TCPIP_CORE_LOCKING == 1
#define LWIP_LOCK() LOCK_TCPIP_CORE()
#define LWIP_UNLOCK() UNLOCK_TCPIP_CORE()
#define LWIP_TRY_LOCK() xSemaphoreTakeL(&lock_tcpip_core,10)
#else
// no locking for ESP32
#define LWIP_LOCK()
#define LWIP_UNLOCK()
#define LWIP_TRY_LOCK()
#endif

#ifdef __cplusplus
}
#endif

#endif
