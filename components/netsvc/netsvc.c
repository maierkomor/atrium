/*
 *  Copyright (C) 2018-2021, Thomas Maier-Komor
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

#define NETSVC_IMPL
#include "netsvc.h"
#include "log.h"
#include "udns.h"
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

#define TAG MODULE_NS
static SemaphoreHandle_t NscSem = 0;

char *Hostname = 0, *Domainname = 0;
uint8_t HostnameLen = 0, DomainnameLen = 0;

#if LWIP_TCPIP_CORE_LOCKING != 1
SemaphoreHandle_t LwipMtx = 0;
#endif
#if LWIP_IPV6
ip6_addr_t IP6G,IP6LL;
#endif


#ifdef CONFIG_UDNS
#else
typedef struct nsc_entry
{
	char *hn;
	ip_addr_t ip;
} nsc_entry_t;

static nsc_entry_t NsCache[8];
#endif


#ifdef CONFIG_UDNS
static void found_hostname_udns(const char *hn, const ip_addr_t *addr, void *arg)
{
	xSemaphoreGive(NscSem);
}

#else
static void found_hostname(const char *hn, const ip_addr_t *addr, void *arg)
{
	static uint8_t overwrite = 0;
	if (hn == 0) {
		log_error(TAG,"found_hostname(0,%p,%p)",addr,arg);
		xSemaphoreGive(NscSem);
		return;
	}
	if (addr == 0) {
		log_dbug(TAG,"unknown host %s",hn);
		xSemaphoreGive(NscSem);
		return;
	}
	log_dbug(TAG,"%s @ %s",inet_ntoa(*addr));

	// update existing
	for (size_t i = 0; i < sizeof(NsCache)/sizeof(NsCache[0]); ++i) {
		if (NsCache[i].hn && (0 == strcmp(NsCache[i].hn,hn))) {
			log_dbug(TAG,"updated ns-cache for %s",hn);
			NsCache[i].ip = *addr;
			xSemaphoreGive(NscSem);
			return;
		}
	}

	// add new
	for (size_t i = 0; i < sizeof(NsCache)/sizeof(NsCache[0]); ++i) {
		if (NsCache[i].hn == 0) {
			log_dbug(TAG,"adding ns-cache for %s",hn);
			NsCache[i].hn = strdup(hn);
			NsCache[i].ip = *addr;
			xSemaphoreGive(NscSem);
			return;
		}
	}

	// overwrite other
	log_dbug(TAG,"overwriting ns-cache %d for %s",overwrite,hn);
	free(NsCache[overwrite].hn);
	NsCache[overwrite].hn = strdup(hn);
	NsCache[overwrite].ip = *addr;
	
	++overwrite;
	if (overwrite == sizeof(NsCache)/sizeof(NsCache[0]))
		overwrite = 0;
	xSemaphoreGive(NscSem);
}


static ip_addr_t *ns_cache_lookup(const char *h)
{
	for (int i = 0; i < sizeof(NsCache)/sizeof(NsCache[0]); ++i) {
		if (NsCache[i].hn && (0 == strcmp(NsCache[i].hn,h))) {
			log_dbug(TAG,"cache hit %s: %s",h,inet_ntoa(NsCache[i].ip));
			return &NsCache[i].ip;
		}
	}
	log_dbug(TAG,"cache miss");
	return 0;
}
#endif


const char *uri_parse(char *path, uri_t *uri)
{
	bzero(uri,sizeof(uri_t));
	if (path[0] == '/') {
		uri->prot = prot_file;
		uri->file = path;
		return 0;
	}
	char *sep = strstr(path,"://");
	if (sep == 0)
		return "Invalid argument.";
	switch (sep-path) {
	case 3:
		if (0 == memcmp(path,"ftp",3)) {
			uri->prot = prot_ftp;
			uri->port = 21;
		}
		break;
	case 4:
		if (0 == memcmp(path,"http",4)) {
			uri->prot = prot_http;
			uri->port = 80;
		} else if (0 == memcmp(path,"tftp",4)) {
			uri->prot = prot_tftp;
			uri->port = 69;
		} else if (0 == memcmp(path,"file",4)) {
			uri->prot = prot_file;
			uri->file = path+6;
			return 0;
		} else if (0 == memcmp(path,"mqtt",4)) {
			uri->prot = prot_mqtt;
			uri->port = 1883;
		}
		break;
	case 5:
		if (0 == memcmp(path,"https",5)) {
			uri->prot = prot_https;
			uri->port = 443;
		}
	default:
		break;
	}
	char *at = strchr(sep+3,'@');
	if (at) {
		uri->user = sep+3;
		*at = 0;
		uri->host = at+1;
		char *pwstr = strchr(sep+3,':');
		if (pwstr) {
			*pwstr = 0;
			uri->pass = pwstr + 1;
		}
	} else {
		uri->host = sep+3;
	}
	char *sl = strchr(uri->host,'/');
	if (0 == sl) {
		uri->file = "/";
	} else {
		*sl = 0;
		uri->file = sl+1;
	}
	char *colon = strchr(uri->host,':');
	if (colon) {
		long l = strtol(colon+1,0,0);
		if ((l <= 0) || (l > UINT16_MAX))
			return "Invalid port";
		uri->port = l;
		*colon = 0;
	}
	err_t e = resolve_hostname(uri->host,&uri->ip);
	if (e)
		return "Unknown host.";
	return 0;
}


int query_host(const char *h, ip_addr_t *ip, void (*cb)(const char *hn, const ip_addr_t *addr, void *arg), void *arg)
{
	if ((h == 0) || (h[0] == 0))
		return -1;
	log_dbug(TAG,"query host %s",h);
	if ((h[0] >= '0') && (h[0] <= '9')) {
		ip_addr_t a;
		if (ipaddr_aton(h,&a)) {
			if (ip)
				*ip = a;
			if (cb) {
				LWIP_LOCK();
				cb(h,&a,arg);
				LWIP_UNLOCK();
			}
			return 0;
		}
	}
#ifdef CONFIG_UDNS
	size_t hl = strlen(h);
	if (0 != memchr(h,'.',hl))
		return udns_query(h,ip,cb,arg);
	if (DomainnameLen == 0) {
		log_warn(TAG,"no domainname set");
		return udns_query(h,ip,cb,arg);
	}
	char hostname[hl+DomainnameLen+2];
	memcpy(hostname,h,hl);
	memcpy(hostname+hl,Domainname-1,DomainnameLen+2);
	return udns_query(hostname,ip,cb,arg);
#else
	return dns_gethostbyname(h,ip,cb,arg);
#endif
}


int resolve_fqhn(const char *h, ip_addr_t *ip)
{
#ifdef CONFIG_UDNS
	if (NscSem == 0)
		NscSem = xSemaphoreCreateCounting(1,0);
	if (0 == udns_query(h,ip,found_hostname_udns,0))
		return 0;
#else
	if (NscSem == 0) {
		memset(NsCache,0,sizeof(NsCache));
		NscSem = xSemaphoreCreateCounting(1,0);
	}
	ip_addr_t *a = ns_cache_lookup(h);
	if (a) {
		*ip = *a;
		return 0;
	}
	esp_err_t e = dns_gethostbyname(h,ip,found_hostname,0);
	if (e == 0) {
		log_dbug(TAG,"hostname %s from dns",h);
		return 0;
	}
#endif
	log_dbug(TAG,"query sent");
	int r = 0;
	if (xSemaphoreTake(NscSem, 2000 / portTICK_PERIOD_MS)) {
#ifdef CONFIG_UDNS
		esp_err_t e = udns_query(h,ip,0,0);
		if (e == 0) {
			char ipstr[64];
			ip2str_r(ip,ipstr,sizeof(ipstr));
			log_dbug(TAG,"hostname %s from dns: %s",h,ipstr);
			return 0;
		}
		r = ESP_ERR_NOT_FOUND;
#else
		a = ns_cache_lookup(h);
		if (a != 0) {
			if (ip_addr_isany(a))
				return 1;
			*ip = *a;
			return 0;
		}
		r = ESP_ERR_NOT_FOUND;
#endif
	} else {
		log_dbug(TAG,"timeout");
		r = ERR_TIMEOUT;
	}
#ifndef CONFIG_UDNS
	struct hostent *he = gethostbyname(h);
	if (he != 0) {
		log_dbug(TAG,"hostname %s from gethostbyname: %s",h,he->h_addr);
		ip->addr = inet_addr(he->h_addr);
		if (ip_addr_isany(ip))
			return 1;
	} else {
		r = ESP_ERR_NOT_FOUND;
	}
	log_dbug(TAG,"host %s: not found",h);
#endif
	return r;
}


int resolve_hostname(const char *h, ip_addr_t *ip)
{
	log_dbug(TAG,"resolve hostname %s",h);
	if ((h[0] >= '0') && (h[0] <= '9')) {
		if (inet_aton(h,ip))
			return 0;
	}
	if (0 != strchr(h,'.'))
		return resolve_fqhn(h,ip);
	size_t hl = strlen(h);
	char buf[strlen(h)+DomainnameLen+7];
	memcpy(buf,h,hl);
	if (DomainnameLen)
		memcpy(buf+hl,Domainname-1,DomainnameLen+2);
	else
		memcpy(buf+hl,".local",7);
	return resolve_fqhn(buf,ip);
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


int setdomainname(const char *dn, size_t l)
{
	assert(Hostname);
	if (l == 0) {
		l = strlen(dn);
		if (l == 0)
			return -EINVAL;
	}
	const char *dot = strchr(dn,'.');
	// no hyphen at beginning or end of domainname
	if ((dot == 0) || (dn[0] == '-') || (dn[l-1] == '-'))
		return -EINVAL;
	const char *x = dn, *e = dn + l;
	char c;
	do {
		c = *x;
		if ((c >= 'a') && (c <= 'z'));
		else if ((c >= '0') && (c <= '9'));
		else if ((c >= 'A') && (c <= 'Z'));
		else if ((c == '-') || (c == '.'));
		else
			return -EINVAL;
		++x;
	} while (x != e);
	char *nh = (char *) malloc(HostnameLen+l+2);	// separating '.' + terminating \0
	memcpy(nh,Hostname,HostnameLen);
	nh[HostnameLen] = '.';
	memcpy(nh+HostnameLen+1,dn,l+1);
	Domainname = nh+HostnameLen+1;
	DomainnameLen = l;
	free(Hostname);
	Hostname = nh;
	log_info(TAG,"domainname %s",Domainname);
	return 0;
}


int sethostname(const char *h, size_t l)
{
	if (l == 0) {
		l = strlen(h);
		if (l == 0)
			return -EINVAL;
	}
	if ((l < 3) || (l > 63))
		return -EINVAL;
	if ((Hostname != 0) && (0 == memcmp(h,Hostname,l)) && (Hostname[l] == '.'))
		return 0;
	const char *x = h, *e = h + l;
	// no hyphen at beginning or end of hostname
	if ((*x == '-') || (e[-1] == '-'))
		return -EINVAL;
	char c;
	do {
		c = *x;
		if ((c >= 'a') && (c <= 'z'));
		else if ((c >= '0') && (c <= '9'));
		else if ((c >= 'A') && (c <= 'Z'));
		else if (c == '-');
		else
			return -EINVAL;
		++x;
	} while (x != e);
	if (DomainnameLen == 0)
		DomainnameLen = 5;
	char *nh = malloc(l+DomainnameLen+2);
	assert(nh);
	memcpy(nh,h,l);
	HostnameLen = l;
	memcpy(nh+l, Domainname ? Domainname-1 : ".local", DomainnameLen+2);
	if (Hostname)
		free(Hostname);
	Hostname = nh;
	Domainname = nh+l+1;
	log_info(TAG,"hostname %.*s",(int)HostnameLen,Hostname);
#ifdef CONFIG_UDNS
	udns_update_hostname();
#endif
	return 0;
}


static const char *LwipErrStr[] = {
	"no error",
	"out of memory",
	"buffer error",
	"timeout",
	"routing error",
	"operation in progress",
	"illegal value",
	"would block",
	"address in use",
	"already connected",
	"already established",
	"not connected",
	"interface error",
	"aborted",
	"connection reset",
	"connection closed",
	"illegal argument",
};


const char *strlwiperr(int e)
{
	if ((e > 0) || (-e > sizeof(LwipErrStr)/sizeof(LwipErrStr[0])))
		return "unknown error";
	return LwipErrStr[-e];
}


const char *ip2str(const ip_addr_t *ip)
{
	if (ip == 0)
		return "<null>";
#if LWIP_IPV6
	if (IP_IS_V6_VAL(*ip)) {
		return ip6addr_ntoa(ip_2_ip6(ip));
	}
#endif
	return ip4addr_ntoa(ip_2_ip4(ip));
}


const char *ip2str_r(const ip_addr_t *ip, char *out, size_t n)
{
	if (ip == 0) {
		strncpy(out,"<null>",n);
		return out;
	}
#if LWIP_IPV6
	if (IP_IS_V6_VAL(*ip)) {
		return ip6addr_ntoa_r(ip_2_ip6(ip),out,n);
	}
#endif
	return ip4addr_ntoa_r(ip_2_ip4(ip),out,n);
}
