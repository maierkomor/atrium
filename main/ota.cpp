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

#include <sdkconfig.h>

#ifdef CONFIG_OTA

#include "globals.h"
#include "leds.h"
#include "log.h"
#include "lwtcp.h"
#include "netsvc.h"
#include "ota.h"
#include "shell.h"
#include "terminal.h"

#ifndef CONFIG_LEDS
#define statusled_set(x)
#endif

#include <esp_system.h>
#include <esp_ota_ops.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#ifdef CONFIG_IDF_TARGET_ESP32
#include <mbedtls/base64.h>
#include <sys/socket.h>
#elif defined CONFIG_IDF_TARGET_ESP8266
extern "C" {
#include <esp_base64.h>
}
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <lwip/ip6.h>
#include <lwip/err.h>
#endif
#include <mbedtls/base64.h>

#include <lwip/dns.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef ESP32
#define OTABUF_SIZE 8192 //(1460*16)
#else
#define OTABUF_SIZE 1460
#endif

#define TAG MODULE_OTA

static char *skip_http_header(Terminal &t, char *text, int len)
{
	char *data = strstr(text,"\r\n\r\n");
	if (data == 0) {
		t.printf("unterminated http header: %d bytes\n",len);
		return 0;
	}
	data += 4;
	data[-1] = 0;
	if (strstr(text,"200 OK\r\n"))
		return data;
	t.printf("http error: %s\n",text);
	return 0;
}


#ifdef CONFIG_SOCKET_API

static int send_http_get(Terminal &t, const char *server, int port, const char *filename, const char *auth)
{
	t.printf("get %s:%d/%s\n", server, port, filename);
	ip_addr_t ip;
	if (err_t e = resolve_hostname(server,&ip)) {
		t.printf("unable to resolve ip of %s: %s\n",server,strlwiperr(e));
		return -1;
	}
	char ipaddr[32];
	inet_ntoa_r(ip,ipaddr,sizeof(ipaddr));
	t.printf("contacting %s",ipaddr);
	int hsock = socket(AF_INET, SOCK_STREAM, 0);
	if (hsock == -1) {
		t.printf("unable to create download socket: %s\n",strerror(errno));
		return -1;
	}
	struct sockaddr_in in;
	struct sockaddr_in6 in6;
	struct sockaddr *a;
	size_t as;
	if (IP_IS_V6(&ip)) {
		in6.sin6_family = AF_INET6;
		in6.sin6_port = htons(port);
		a = (struct sockaddr *) &in6;
		as = sizeof(struct sockaddr_in6);
		memcpy(&in6.sin6_addr,ip_2_ip6(&ip),sizeof(in6.sin6_addr));
	} else {
		in.sin_family = AF_INET;
		in.sin_port = htons(port);
		a = (struct sockaddr *) &in;
		as = sizeof(struct sockaddr_in);
		memcpy(&in.sin_addr,ip_2_ip4(&ip),sizeof(in.sin_addr));
	}
	if (-1 == connect(hsock,a,as)) {
		t.printf("connect to %s failed: %s\n",inet_ntoa(ip),strerror(errno));
		close(hsock);
		return -1;
	}
	t.printf("connected to %s\n",inet_ntoa(ip));
	char http_request[512];
	int get_len = snprintf(http_request,sizeof(http_request),
		"GET /%s HTTP/1.0\r\n"
		"Host: %s:%d\r\n"
		"User-Agent: atrium\r\n"
		, filename, server, port);
	if (get_len+3 > sizeof(http_request))
		return -1;
	if (auth) {
		strcpy(http_request+get_len,"Authorization: Basic ");
		get_len += 21;
#ifdef CONFIG_IDF_TARGET_ESP8266
		int n = esp_base64_encode(auth,strlen(auth),http_request+get_len,sizeof(http_request)-get_len);
		if (n < 0) {
			t.printf("base64: 0x%x\n",n);
			return -1;
		}
		get_len += n;
#else
		size_t b64l = sizeof(http_request)-get_len;
		int n = mbedtls_base64_encode((unsigned char *)http_request+get_len,sizeof(http_request)-get_len,&b64l,(unsigned char *)auth,strlen(auth));
		if (0 > n) {
			t.printf("base64: 0x%x\n",n);
			return -1;
		}
		get_len += b64l;
#endif
		strcpy(http_request+get_len,"\r\n");
		get_len += 2;
	}
	if (get_len + 3 > sizeof(http_request))
		return -1;
	strcpy(http_request+get_len,"\r\n");
	get_len += 2;
	//t.printf("http request:\n'%s'\n",http_request);
	int res = send(hsock, http_request, get_len, 0);
	if (res < 0) {
		t.printf("unable to send get request: %s\n",strerror(errno));
    		return -1;
	}
	return hsock;
}

#else // ESP8266

static const char *send_http_get(Terminal &t, LwTcp &P, const char *server, int port, const char *filename, const char *auth)
{
	t.printf("get: %s, auth=%s\n", filename, auth ? auth : "0");
	char http_request[512];
	int get_len = snprintf(http_request,sizeof(http_request),
		"GET /%s HTTP/1.0\r\n"
		"Host: %s:%d\r\n"
		"User-Agent: atrium\r\n"
		, filename, server, port);
	if (get_len+3 > sizeof(http_request))
		return "Header too long.";
	if (auth) {
		strcpy(http_request+get_len,"Authorization: Basic ");
		get_len += 21;
#ifdef CONFIG_IDF_TARGET_ESP8266
		int n = esp_base64_encode(auth,strlen(auth),http_request+get_len,sizeof(http_request)-get_len);
		if (n < 0) {
			t.printf("base64: 0x%x\r\n",n);
			return "";
		}
		get_len += n;
#else
		size_t b64l = sizeof(http_request)-get_len;
		int n = mbedtls_base64_encode((unsigned char *)http_request+get_len,sizeof(http_request)-get_len,&b64l,(unsigned char *)auth,strlen(auth));
		if (0 > n) {
			t.printf("base64: 0x%x\r\n",n);
			return "";
		}
		get_len += b64l;
#endif
		strcpy(http_request+get_len,"\r\n");
		get_len += 2;
	}
	if (get_len + 3 > sizeof(http_request))
		return "Out of memory.";
	strcpy(http_request+get_len,"\r\n");
	get_len += 2;
	//t.printf("http request:\n'%s'\n",http_request);
	int res = P.write(http_request,get_len);
	if (res < 0) {
		t.printf("unable to send get: %s\n",P.error());
    		return "";
	}
	return 0;
}

#endif

static const char *to_fd(Terminal &t, void *arg, char *buf, size_t s)
{
	//t.printf("to_fd(): %u bytes\n",s);
	if (-1 != write((int)arg,buf,s))
		return 0;
	return strerror(errno);
}


static const char *to_ota(Terminal &t, void *arg, char *buf, size_t s)
{
	esp_err_t err = esp_ota_write((esp_ota_handle_t)arg,buf,s);
	if (err == ESP_OK)
		return 0;
	t.printf("OTA flash: %s\n",esp_err_to_name(err));
	return "";
}


#ifdef CONFIG_SOCKET_API
static int socket_to_x(Terminal &t, int hsock, const char *(*sink)(Terminal&,void*,char*,size_t), void *arg)
{
	char *buf = (char*)malloc(OTABUF_SIZE), *data;
	if (buf == 0)
		return "Out of memory.";
	int ret = "Failed.";
	size_t numD = 0;
	int r = recv(hsock,buf,OTABUF_SIZE,0);
	if (r < 0)
		goto done;
	log_dbug(TAG,"header:\n%.*s",r,buf);
	if (memcmp(buf,"HTTP",4) || memcmp(buf+8," 200 OK\r",8)) {
		char *nl = strchr(buf,'\n');
		if (nl) {
			*nl = 0;
			t.printf("unexpted answer: %s\n",buf);
		}
		goto done;
	}
	data = skip_http_header(t,buf,r);
	if (data == 0) {
		ret = "No end of header.";
		goto done;
	}
	r -= (data-buf);
	while (r > 0) {
		numD += r;
		if (sink(t,arg,data,r))
			goto done;
		r = recv(hsock,buf,OTABUF_SIZE,0);
		data = buf;
		t.printf("wrote %d bytes\r",numD);
	}
	t.println();
	ret = 0;
done:
	free(buf);
	return ret;
}

#else // if LWTCP

static const char *socket_to_x(Terminal &t, LwTcp &P, const char *(*sink)(Terminal&,void*,char*,size_t), void *arg)
{
	char *buf = (char*)malloc(OTABUF_SIZE), *data;
	if (buf == 0)
		return "Out of memory.";
	const char *ret = "Failed.";
	size_t numD = 0, contlen = 0;
	int r = P.read(buf,OTABUF_SIZE);
	if (r < 0)
		goto done;
	log_dbug(TAG,"header:\n%.*s",r,buf);
	if (memcmp(buf,"HTTP",4) || memcmp(buf+8," 200 OK\r",8)) {
		if (0 == memcmp(buf+8," 404 ",5)) {
			ret = "server 404: file not found";
			goto done;
		}
		if (char *nl = strchr(buf,'\n'))
			*nl = 0;
		t.printf("unexpected answer: %s\n",buf);
		ret = "";
		goto done;
	}
	if (const char *cl = strstr(buf+8,"Content-Length:")) {
		contlen = strtol(cl+16,0,0);
		t.printf("content-length %u\n",contlen);
	}
	if (contlen == 0)
		goto done;
	data = skip_http_header(t,buf,r);
	if (data == 0) {
		ret = "no end of header";
		goto done;
	}
	r -= (data-buf);
	while (r > 0) {
		if (const char *e = sink(t,arg,data,r)) {
			ret = e;
			goto done;
		}
		numD += r;
		t.printf("\rwrote %d/%u bytes",numD,contlen);
		t.sync(false);
		if (numD == contlen)
			break;
		r = P.read(buf,OTABUF_SIZE,60000);
		data = buf;
	}
	if (r < 0) {
		t.println(P.error());
	} else if (numD != contlen) {
		ret = "\nincomplete";
	} else {
		ret = 0;
	}
done:
	free(buf);
	return ret;
}

#endif


#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
static const char *ftp_to(Terminal &t, char *addr, const char *(*sink)(Terminal &,void*,char*,size_t), void *arg)
{
	const char *server;
	t.printf("download %s\n",addr);
	if (0 == strncmp(addr,"ftp://",6)) {
		server = addr + 6;
	} else {
		t.printf("invalid address '%s'\n",addr);
		return "";
	}
	//  user/pass extraction
	uint16_t port = 21;
	const char *username = "ftp";
	const char *pass = "ftp";
	char *at = strchr(server,'@');
	if (at) {
		*at = 0;
		username = server;
		if (char *colon = strchr(server,':')) {
			*colon = 0;
			pass = colon + 1;
		} else {
			pass = 0;
		}
		server = at + 1;
	}

	char *filepath = strchr(server,'/');
	if (filepath == 0)
		return "Invalid argument.";
	*filepath = 0;
	++filepath;
	char *portstr = strchr(server,':');
	if (portstr) {
		*portstr = 0;
		long l = strtol(++portstr,0,0);
		if ((l <= 0) || (l > UINT16_MAX)) {
			return "invalid port";
		}
		port = l;
	}
	ip_addr_t ip;
	t.printf("resolve '%s'\n",server);
	if (err_t e = resolve_hostname(server,&ip)) {
		t.printf("unable to resolve ip of %s: %s\n",server,strlwiperr(e));
		return "";
	}
	LwTcp P;
	char tmp[128];
	ipaddr_ntoa_r(&ip,tmp,sizeof(tmp));
	t.printf("connect %s:%d\n",tmp,port);
	if (P.connect(&ip,port))
		return P.error();
	int n = P.read(tmp,sizeof(tmp),5000/portTICK_PERIOD_MS);
	if (n < 0)
    		return P.error();
	if ((n < 4) || (0 != strncmp(tmp,"220 ",4)))
		return "Protocol error.";
	t.println("Connected.");

	size_t ul = strlen(username);
	if (ul > 122)
		return "Invalid argument.";
	memcpy(tmp,"USER ",5);
	memcpy(tmp+5,username,ul);
	tmp[ul+5] = '\r';
	tmp[ul+6] = '\n';
	tmp[ul+7] = 0;
//	log_dbug(TAG,"send '%.*s'",ul+5,tmp);
	int res = P.write(tmp,ul+7);
	if (res < 0)
    		return P.error();
	n = P.read(tmp,sizeof(tmp),5000/portTICK_PERIOD_MS);
	if (n < 0)
    		return P.error();
//	log_dbug(TAG,"rcvd '%.*s'",n,tmp);
	if ((n < 4) || ((0 != strncmp(tmp,"331 ",4)) && (0 != strncmp(tmp,"230 ",4))))
		return "Login rejected.";
	if (pass) {
		size_t pl = strlen(pass);
		if (pl > 122)
			return "Invalid argument.";
		memcpy(tmp,"PASS ",5);
		memcpy(tmp+5,pass,pl);
		tmp[pl+5] = '\r';
		tmp[pl+6] = '\n';
		res = P.write(tmp,pl+7);
		if (res < 0)
			return P.error();
		log_dbug(TAG,"send '%.*s'",pl+5,tmp);
		n = P.read(tmp,sizeof(tmp),5000/portTICK_PERIOD_MS);
		if (n < 0)
			return P.error();
		log_dbug(TAG,"rcvd '%.*s'",n,tmp);
		if ((n < 4) || ((0 != strncmp(tmp,"331 ",4)) && (0 != strncmp(tmp,"230 ",4))))
			return "Login rejected.";
	}
	res = P.write("PASV\r\n",6);
	if (res < 0)
		return P.error();
	log_dbug(TAG,"send 'PASV'");
	n = P.read(tmp,sizeof(tmp),5000/portTICK_PERIOD_MS);
	if (n < 0)
		return P.error();
	log_dbug(TAG,"rcvd '%.*s'",n,tmp);
	unsigned pasv[6];
	n = sscanf(tmp,"227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)."
			,pasv+0,pasv+1,pasv+2,pasv+3,pasv+4,pasv+5);
	if (n != 6)
		return "Connection failed.";
	sprintf(tmp,"%u.%u.%u.%u",pasv[0],pasv[1],pasv[2],pasv[3]);
	uint16_t dp = pasv[4] << 8 | pasv[5];
	t.println(tmp);
	ip_addr_t dip;
	if (0 == ipaddr_aton(tmp,&dip))
		return "Address error.";
	LwTcp D;
	if (D.connect(&dip,dp)) {
		t.println(D.error());
		return "PASV connect failed.";
	}
	memcpy(tmp,"TYPE I\r\nRETR ",13);
	size_t fl = strlen(filepath);
	memcpy(tmp+13,filepath,fl);
	tmp[fl+13] = '\r';
	tmp[fl+14] = '\n';
	res = P.write(tmp,fl+15);
	if (res < 0)
		return P.error();
//	t.printf("send '%.*s'\n",fl+13,tmp);
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0)
		return "Out of memory.";
	const char *ret = 0;
	unsigned total = 0;
	n = D.read(buf,OTABUF_SIZE);
	while (n > 0) {
		total += n;
		t.printf("\rread %d",total);
		if (sink(t,arg,buf,n)) {
			ret = "";
			break;
		}
		n = D.read(buf,OTABUF_SIZE);
	}
	t.println("\ndone");
	P.write("QUIT\r\n",6);
	free(buf);
	return ret;
}


static const char *file_to_flash(Terminal &t, int fd, esp_ota_handle_t ota)
{
	size_t numD = 0;
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0)
		return "Out of memory.";
	bool ia = t.isInteractive();
	const char *r = "Failed.";
	for (;;) {
		int n = read(fd,buf,OTABUF_SIZE);
		if (n < 0) {
			t.printf("OTA read error: %s\n",strerror(errno));
			break;
		}
		if (n == 0) {
			//t.printf("ota received %d bytes, wrote %d bytes",numD,numU);
			r = 0;
			break;
		}
		numD += n;
		esp_err_t err = esp_ota_write(ota,buf,n);
		if (err != ESP_OK) {
			t.printf("OTA write failed: %s\n",esp_err_to_name(err));
			break;
		}
		if (ia)
			t.printf("\rwrote %d bytes",r);
	}
	if (ia)
		t.println();
	free(buf);
	return r;
}
#endif	// CONFIG_ESPTOOLPY_FLASHSIZE_1MB


static const char *http_to(Terminal &t, char *addr, const char *(*sink)(Terminal &,void*,char*,size_t), void *arg)
{
	uint16_t port;
	const char *server;
	t.printf("download %s\n",addr);
	if (0 == strncmp(addr,"http://",7)) {
		port = 80;
		server = addr + 7;
	} else if (0 == strncmp(addr,"https://",8)) {
		port = 443;
		server = addr + 8;
	} else {
		t.printf("invalid address '%s'\n",addr);
		return "";
	}
	//  user/pass extraction
	const char *username = 0;
	char *at = strchr(server,'@');
	if (at) {
		*at = 0;
		username = server;
		server = at + 1;
		// Username+password are supplied separated by colon and encoded like this.
		// user+password must be BASE64 encoded for passing to
		// the HTTP/GET request with authorization basic
	}

	char *filepath = strchr(server,'/');
	if (filepath == 0)
		return "Invalid argument.";
	*filepath = 0;
	++filepath;
	char *portstr = strchr(server,':');
	if (portstr) {
		*portstr = 0;
		long l = strtol(++portstr,0,0);
		if ((l <= 0) || (l > UINT16_MAX)) {
			return "invalid port";
		}
		port = l;
	}
#ifdef CONFIG_SOCKET_API
	int hsock = send_http_get(t,server,port,filepath,username);
	if (hsock == -1)
		return "Failed.";
	const char *r = socket_to_x(t,hsock,sink,arg);
	close(hsock);
#else // ESP8266
	ip_addr_t ip;
	if (err_t e = resolve_hostname(server,&ip)) {
		t.printf("unable to resolve ip of %s: %s\n",server,strlwiperr(e));
		return "Unknown host";
	}
	LwTcp P;
	P.connect(&ip,port);
	const char *r = send_http_get(t,P,server,port,filepath,username);
	if (r == 0)
		r = socket_to_x(t,P,sink,arg);
#endif
	return r;
}


const char *http_download(Terminal &t, char *addr, const char *fn)
{
	if (fn == 0) {
		 char *sl = strrchr(addr,'/');
		 if (sl == 0) {
			 t.printf("invalid address '%s'\n",addr);
			 return "Failed.";
		 }
		 fn = sl+1;
	}
	int fd;
	if (fn[0] != '/') {
		const char *pwd = t.getPwd().c_str();
		size_t l = strlen(pwd)+strlen(fn)+1;
		if (l > 128)
			return "Failed.";
		char path[l];
		strcpy(path,pwd);
		strcat(path,fn);
		fd = creat(path,0666);
	} else {
		fd = creat(fn,0666);
	}
	if (fd == -1) {
		t.printf("error creating %s\n",fn);
		return strerror(errno);
	}
	t.printf("downloading to %s\n",fn);
	const char *r = http_to(t,addr,to_fd,(void*)fd);
	close(fd);
	return r;
}


static const char *to_part(Terminal &t, void *arg, char *buf, size_t s)
{
	esp_partition_t *p = (esp_partition_t *)arg;
	//t.printf("to_part %u@%x\n",s,*addr);
	if (s > p->size)
		return "End of partition.";
	esp_err_t e = spi_flash_write(p->address,buf,s);
	if (e) {
		t.printf("flash error %s\n",esp_err_to_name(e));
		return "";
	}
	p->address += s;
	p->size -= s;
	return 0;
}


const char *update_part(Terminal &t, char *source, const char *dest)
{
	esp_partition_t *p = (esp_partition_t *) esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,dest);
	if (p == 0) {
		p = (esp_partition_t *) esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_ANY,dest);
		if (p == 0) {
			return "unknown partition";
		}
	}
	const esp_partition_t *b = esp_ota_get_running_partition();
	/*
	 * 	only 2 valid situations:
		[Bs Be] [Ps Pe]: checked with (Be <= Ps) // implies (Bs < Ps)
		[Ps Pe] [Bs Be]: checked with (Pe <= Bs) // implies (Pe <= Bs)
		invalid overlaps are implicitly checked
		[Bs [Ps Be] Pe]: invalid, violates both rules
		[Bs [Ps Pe] Be]: invalid, violates both rules
		[Ps [Bs Pe] Be]: invalid, violates both rules
		[Ps [Bs Pe] Be]: invalid, violates both rules
		[Ps [Bs Be] Pe]: invalid, violates both rules
		[Bs [Ps Pe] Be]: invalid, violates both rules
		[Ps [Bs Be] Pe]: invalid, violates both rules
	*/
	unsigned Bs = b->address;
	unsigned Be = b->address+b->size;
	unsigned Ps = p->address;
	unsigned Pe = p->address+p->size;
	if (! ((Be <= Ps) || (Pe <= Bs))) {
		return "Cannot update active partition.";
	}
	uint32_t addr = p->address;
	uint32_t s = p->size;
	t.printf("erasing %d@%x\n",p->size,p->address);
	t.sync();
	if (esp_err_t e = spi_flash_erase_range(p->address,p->size)) {
		t.println("error erasing");
		return esp_err_to_name(e);
	}
	const char *r;
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	if (0 == strncmp(source,"ftp://",6))
		r = ftp_to(t,source,to_part,(void*)p);
	else
#endif
		r = http_to(t,source,to_part,(void*)p);
	p->address = addr;
	p->size = s;
	return r;
}


const char *perform_ota(Terminal &t, char *source, bool changeboot)
{
	statusled_set(ledmode_pulse_often);
	vTaskPrioritySet(0,10);
	bool ia = t.isInteractive();
	const esp_partition_t *bootp = esp_ota_get_boot_partition();
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	const esp_partition_t *runp = esp_ota_get_running_partition();
	if (ia)
		t.printf("running on '%s' at 0x%08x\n"
			,runp->label
			,runp->address);
	if (bootp != runp)
		t.printf("boot partition: '%s' at 0x%08x\n",bootp->label,bootp->address);
#endif
	const char *result = "Failed.";
	esp_ota_handle_t ota = 0;
	const esp_partition_t *updatep = esp_ota_get_next_update_partition(NULL);
	if (updatep == 0) {
		t.println("no OTA partition");
		goto failed;
	}
	if (ia) {
		t.printf("erasing '%s' at 0x%08x\n",updatep->label,updatep->address);
		t.sync();
	}
	if (esp_err_t err = esp_ota_begin(updatep, OTA_SIZE_UNKNOWN, &ota)) {
		t.printf("OTA begin: %s\n",esp_err_to_name(err));
		goto failed;
	}

	if ((0 == memcmp(source,"http://",7)) || (0 == memcmp(source,"https://",8))) {
		result = http_to(t,source,to_ota,(void*)ota);
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	} else if (0 == memcmp(source,"ftp://",6)) {
		result = ftp_to(t,source,to_ota,(void*)ota);
#endif
	} else {
#ifdef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
		return "Invalid OTA link.";
#else
		t.printf("open file %s\n",source);
		int fd = open(source,O_RDONLY);
		if (fd == -1) {
			t.printf("open %s\n",source);
			return strerror(errno);
		}
		result = file_to_flash(t,fd,ota);
		close(fd);
#endif
	}
	if (result)
		goto failed;
	t.println("verify");
	t.sync();
	if (esp_err_t e = esp_ota_end(ota)) {
		log_warn(TAG,"esp_ota_end: %s\n",esp_err_to_name(e));
		t.printf("ota verify: %s\n",esp_err_to_name(e));
		goto failed;
	}
	if (changeboot && (bootp != updatep)) {
		int err = esp_ota_set_boot_partition(updatep);
		if (err != ESP_OK) {
			t.printf("set boot failed: %s\n",esp_err_to_name(err));
			goto failed;
		}
	}
	statusled_set(ledmode_off);
	return 0;
failed:
	statusled_set(ledmode_pulse_seldom);
	return result;
}


const char *ota_from_server(Terminal &term, const char *server, const char *version)
{
	if ((server[0] == 0) || strncmp(server,"http://",7))
		return "OTA server invalid";
#ifdef ESP32
	const char *fext = "bin";
#else
	const esp_partition_t *u = esp_ota_get_next_update_partition(NULL);
	if (u == 0)
		return "No update partition.";
	const char *fext = u->label;
#endif
	char src[strlen(server)+strlen(version)+32];
	snprintf(src,sizeof(src),"%s/%s/atrium-%s.%s",server,FwCfg,version,fext);
	return perform_ota(term,src,true);
}


const char *boot(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return "Invalid number of arguments.";
	if (argc == 1) {
		const esp_partition_t *b = esp_ota_get_boot_partition();
		const esp_partition_t *r = esp_ota_get_running_partition();
		const esp_partition_t *u = esp_ota_get_next_update_partition(NULL);
		term.printf(
			"boot  : %s\n"
			"run   : %s\n"
			"update: %s\n"
			, b ? b->label : "<error>"
			, r ? r->label : "<error>"
			, u ? u->label : "<error>"
		);
		return (b && r) ? 0 : "Failed.";
	}
	if (0 == term.getPrivLevel())
		return "Access denied.";
	esp_partition_iterator_t i = esp_partition_find(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_ANY,0);
	while (i) {
		const esp_partition_t *p = esp_partition_get(i);
		if (0 == strcmp(p->label,args[1])) {
			esp_ota_set_boot_partition(p);
			esp_partition_iterator_release(i);
			return 0;
		}
		i = esp_partition_next(i);
	}
	printf("Cannot boot %s.\n",args[1]);
	return "Failed.";
}

#endif
