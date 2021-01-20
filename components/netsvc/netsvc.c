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

#include "netsvc.h"
#include "log.h"
#ifndef IDF_VERSION
#include "versions.h"
#endif

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <esp_err.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <lwip/err.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <netdb.h>
#ifdef CONFIG_MDNS
#include <mdns.h>
#endif

typedef struct nsc_entry
{
	char *hn;
	ip4_addr_t ip4;
} nsc_entry_t;

static nsc_entry_t NsCache[8];
static char TAG[] = "ns";
static SemaphoreHandle_t NscSem = 0;


static void found_hostname(const char *hn, const ip_addr_t *addr, void *arg)
{
	static uint8_t overwrite = 0;
	if (hn == 0) {
		log_error(TAG,"found_hostname(0,%p,%p)",addr,arg);
		xSemaphoreGive(NscSem);
		return;
	}
	if (addr == 0) {
		//log_warn(TAG,"unknown host %s",hn);
		xSemaphoreGive(NscSem);
		return;
	}
#if defined CONFIG_LWIP_IPV6 || defined CONFIG_IDF_TARGET_ESP32
	if (addr->type != IPADDR_TYPE_V4) {
		log_warn(TAG,"got unspported address type %d for %s",addr->type,hn);
		xSemaphoreGive(NscSem);
		return;
	}
	uint32_t a = addr->u_addr.ip4.addr;
#else
	uint32_t a = addr->addr;
#endif
	log_dbug(TAG,"%s @ %d.%d.%d.%d"
			, hn
			, a & 0xff
			, (a >> 8) & 0xff
			, (a >> 16) & 0xff
			, (a >> 24) & 0xff
		);

	// update existing
	for (size_t i = 0; i < sizeof(NsCache)/sizeof(NsCache[0]); ++i) {
		if (NsCache[i].hn && (0 == strcmp(NsCache[i].hn,hn))) {
			log_dbug(TAG,"updated ns-cache for %s",hn);
			NsCache[i].ip4.addr = a;
			xSemaphoreGive(NscSem);
			return;
		}
	}

	// add new
	for (size_t i = 0; i < sizeof(NsCache)/sizeof(NsCache[0]); ++i) {
		if (NsCache[i].hn == 0) {
			log_dbug(TAG,"adding ns-cache for %s",hn);
			NsCache[i].hn = strdup(hn);
			NsCache[i].ip4.addr = a;
			xSemaphoreGive(NscSem);
			return;
		}
	}

	// overwrite other
	log_dbug(TAG,"overwriting ns-cache %d for %s",overwrite,hn);
	free(NsCache[overwrite].hn);
	NsCache[overwrite].hn = strdup(hn);
	NsCache[overwrite].ip4.addr = a;
	
	++overwrite;
	if (overwrite == sizeof(NsCache)/sizeof(NsCache[0]))
		overwrite = 0;
	xSemaphoreGive(NscSem);
}


static uint32_t ns_cache_lookup(const char *h)
{
	for (int i = 0; i < sizeof(NsCache)/sizeof(NsCache[0]); ++i) {
		if (NsCache[i].hn && (0 == strcmp(NsCache[i].hn,h))) {
			log_dbug(TAG,"cache hit %s",h);
			return NsCache[i].ip4.addr;
		}
	}
	return IPADDR_NONE;
}


uint32_t resolve_fqhn(const char *h)
{
	if ((h[0] >= '0') && (h[0] <= '9')) {
		uint32_t i4 = ipaddr_addr(h);
		if (i4 != IPADDR_NONE)
			return i4;
	}
	if (NscSem == 0) {
		memset(NsCache,0,sizeof(NsCache));
		NscSem = xSemaphoreCreateCounting(1,0);
	}
	uint32_t a = ns_cache_lookup(h);
	if (a != IPADDR_NONE)
		return a;
	ip_addr_t ip;
	esp_err_t e = dns_gethostbyname(h,&ip,found_hostname,0);
	if (e == 0) {
		log_dbug(TAG,"hostname %s from dns",h);
#if defined CONFIG_LWIP_IPV6 || defined CONFIG_IDF_TARGET_ESP32
		return ip.u_addr.ip4.addr;
#else
		return ip.addr;
#endif
	}
	if (xSemaphoreTake(NscSem, 2000 / portTICK_PERIOD_MS)) {
		a = ns_cache_lookup(h);
		if (a != IPADDR_NONE)
			return a;
	}
#ifdef CONFIG_MDNS
	ip4_addr_t ip4;
	e = mdns_query_a(h,1,&ip4);
	if (e == 0) {
		log_dbug(TAG,"mdns resolve hostname %s",h);
		return ip4.addr;
	}
#endif
	struct hostent *he = gethostbyname(h);
	if (he != 0) {
		log_dbug(TAG,"hostname %s from gethostbyname",h);
		return inet_addr(he->h_addr);
	}
	log_dbug(TAG,"host %s: not found",h);
	return 0;
}


const char *strneterr(int socket)
{
	int errcode;
	uint32_t optlen = sizeof(errcode);
	int err = getsockopt(socket, SOL_SOCKET, SO_ERROR, &errcode, &optlen);
	if (err == -1)
		return "error while retriving error string";
	const char *errstr = strerror(errcode);
	return errstr ? errstr : "unknown error";
}


