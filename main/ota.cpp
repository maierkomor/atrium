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

#ifdef CONFIG_OTA

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

#ifdef CONFIG_IDF_TARGET_ESP32
#define OTABUF_SIZE 8192
#else
#define OTABUF_SIZE 2048
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
	//t.printf("header:\n%s\n",text);
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
		memcpy(&in6.sin6_addr,ip_2_ip6(&ip),sizeof(in6));
	} else {
		in.sin_family = AF_INET;
		in.sin_port = htons(port);
		a = (struct sockaddr *) &in;
		as = sizeof(struct sockaddr_in);
		memcpy(&in.sin_addr,ip_2_ip4(&ip),sizeof(in));
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

static int send_http_get(Terminal &t, LwTcp &P, const char *server, int port, const char *filename, const char *auth)
{
	log_dbug(TAG,"get: %s, auth=%s\n", filename, auth ? auth : "0");
	t.printf("get: %s, auth=%s\n", filename, auth ? auth : "0");
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
	int res = P.write(http_request,get_len);
	if (res < 0) {
		log_warn(TAG,"unable to send get: %s\n",P.error());
		t.printf("unable to send get: %s\n",P.error());
    		return -1;
	}
	t.println("sent GET");
	return res;
}

#endif

static int to_fd(Terminal &t, void *arg, char *buf, size_t s)
{
	//t.printf("to_fd(): %u bytes\n",s);
	if (-1 != write((int)arg,buf,s))
		return 0;
	t.printf("write error: %s\n",strerror(errno));
	return 1;
}


static int to_ota(Terminal &t, void *arg, char *buf, size_t s)
{
	esp_err_t err = esp_ota_write((esp_ota_handle_t)arg,buf,s);
	if (err == ESP_OK) {
		return 0;
	}
	t.printf("OTA flash: %s\n",esp_err_to_name(err));
	log_warn(TAG,"OTA flash: %s\n",esp_err_to_name(err));
	return 1;
}


#ifdef CONFIG_SOCKET_API
static int socket_to_x(Terminal &t, int hsock, int (*sink)(Terminal&,void*,char*,size_t), void *arg)
{
	char *buf = (char*)malloc(OTABUF_SIZE), *data;
	if (buf == 0) {
		t.println("out of memory");
		return 1;
	}
	int ret = 1;
	size_t numD = 0;
	int r = recv(hsock,buf,OTABUF_SIZE,0);
	if (r < 0)
		goto done;
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
		t.println("no end of header");
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
	t.printf("\n");
	ret = 0;
done:
	free(buf);
	return ret;
}

#else // if ESP8266

static int socket_to_x(Terminal &t, LwTcp &P, int (*sink)(Terminal&,void*,char*,size_t), void *arg)
{
	char *buf = (char*)malloc(OTABUF_SIZE), *data;
	if (buf == 0) {
		t.println("Out of memory.");
		return 1;
	}
	int ret = 1;
	size_t numD = 0, contlen = 0;
	bool ia = t.isInteractive();
	int r = P.read(buf,OTABUF_SIZE);
	const char *result = 0;
	if (r < 0)
		goto done;
	if (memcmp(buf,"HTTP",4) || memcmp(buf+8," 200 OK\r",8)) {
		if (0 == memcmp(buf+8," 404 ",5)) {
			result = "server 404: file not found";
			goto done;
		}
		t.printf("unexpected header\n%128s\n",buf);
		char *nl = strchr(buf,'\n');
		if (nl) {
			*nl = 0;
			t.printf("unexpted answer: %s\n",buf);
		}
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
		result = "no end of header";
		goto done;
	}
	r -= (data-buf);
	while (r > 0) {
		log_dbug(TAG,"sink %u",r);
		if (int e = sink(t,arg,data,r)) {
			log_warn(TAG,"sink error %d",e);
			goto done;
		}
		numD += r;
		if (ia) {
			t.printf("\rwrote %d/%u bytes",numD,contlen);
			t.sync(false);
		}
		if (numD == contlen)
			break;
		r = P.read(buf,OTABUF_SIZE,60000);
		data = buf;
	}
	if (r < 0) {
		t.printf("error %d",r);
		log_warn(TAG,"read error %d",r);
	} else if (numD != contlen) {
		result = "\nincomplete";
		log_warn(TAG,"incomplete");
	} else {
		ret = 0;
	}
done:
	if (ia)
		t.println(result);
	P.close();
	free(buf);
	return ret;
}

#endif

static int file_to_flash(Terminal &t, int fd, esp_ota_handle_t ota)
{
	size_t numD = 0;
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0) {
		t.println("Out of memory.");
		return 1;
	}
	bool ia = t.isInteractive();
	int r = 1;
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
			t.printf("ota write failed: %s\n",esp_err_to_name(err));
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


static int http_to(Terminal &t, char *addr, int (*sink)(Terminal &,void*,char*,size_t), void *arg)
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
		return 1;
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
	if (filepath == 0) {
		t.println("no file in http address");
		return 1;
	}
	*filepath = 0;
	++filepath;
	char *portstr = strchr(server,':');
	if (portstr) {
		long l = strtol(++portstr,0,0);
		if ((l < 0) || (l > UINT16_MAX)) {
			t.println("invalid port");
			return false;
		}
		port = l;
	}
	ip_addr_t ip;
	if (err_t e = resolve_hostname(server,&ip)) {
		t.printf("unable to resolve ip of %s: %s\n",server,strlwiperr(e));
		return -1;
	}
#ifdef CONFIG_SOCKET_API
	int hsock = send_http_get(t,server,port,filepath,username);
	if (hsock == -1)
		return 1;
	int r = socket_to_x(t,hsock,sink,arg);
	close(hsock);
#else // ESP8266
	LwTcp P;
	P.connect(&ip,port);
	int r = send_http_get(t,P,server,port,filepath,username);
	if (r >= 0)
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
		const char *pwd = getpwd();
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
	int r = http_to(t,addr,to_fd,(void*)fd);
	close(fd);
	return r ? "Failed." : 0;
}


static int to_part(Terminal &t, void *arg, char *buf, size_t s)
{
	esp_partition_t *p = (esp_partition_t *)arg;
	//t.printf("to_part %u@%x\n",s,*addr);
	if (s > p->size) {
		t.println("end of partition");
		return 1;
	}
	esp_err_t e = spi_flash_write(p->address,buf,s);
	if (e) {
		t.printf("flash error %s\n",esp_err_to_name(e));
		return 1;
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
		return "cannot update active partition";
	}
	uint32_t addr = p->address;
	uint32_t s = p->size;
	t.printf("erasing %d@%x\n",p->size,p->address);
	t.sync();
	log_dbug(TAG,"erase");
	if (esp_err_t e = spi_flash_erase_range(p->address,p->size)) {
		t.println("error erasing");
		return esp_err_to_name(e);
	}
	int r = http_to(t,source,to_part,(void*)p);
	p->address = addr;
	p->size = s;
	return r ? "Failed." : 0;
}


const char *perform_ota(Terminal &t, char *source, bool changeboot)
{
	statusled_set(ledmode_pulse_often);
	vTaskPrioritySet(0,10);
	const esp_partition_t *bootp = esp_ota_get_boot_partition();
	const esp_partition_t *runp = esp_ota_get_running_partition();
	bool ia = t.isInteractive();
	if (ia)
		t.printf("running on '%s' at 0x%08x\n"
			,runp->label
			,runp->address);
	if (bootp != runp)
		t.printf("boot partition: '%s' at 0x%08x\n",bootp->label,bootp->address);
	const esp_partition_t *updatep = esp_ota_get_next_update_partition(NULL);
	if (updatep == 0) {
		t.println("no OTA partition");
		statusled_set(ledmode_pulse_seldom);
		return "Failed.";
	}
	if (ia) {
		t.printf("erasing '%s' at 0x%08x\n",updatep->label,updatep->address);
		t.sync();
	}
	log_dbug(TAG,"erase");
	esp_ota_handle_t ota = 0;
	esp_err_t err = esp_ota_begin(updatep, OTA_SIZE_UNKNOWN, &ota);
	if (err != ESP_OK) {
		t.printf("esp_ota_begin failed: %s\n",esp_err_to_name(err));
		statusled_set(ledmode_pulse_seldom);
		return "Failed.";
	}

	int result;
	if ((0 == memcmp(source,"http://",7)) || (0 == memcmp(source,"https://",8))) {
		result = http_to(t,source,to_ota,(void*)ota);
	} else {
		t.printf("open file %s\n",source);
		int fd = open(source,O_RDONLY);
		if (fd == -1) {
			t.printf("open %s\n",source);
			return strerror(errno);
		}
		result = file_to_flash(t,fd,ota);
		close(fd);
	}
	if (result)
		return "Failed.";
	t.printf("verify ota\n");
	t.sync();
	if (esp_err_t e = esp_ota_end(ota)) {
		log_warn(TAG,"esp_ota_end: %s\n",esp_err_to_name(e));
		t.printf("ota verify: %s\n",esp_err_to_name(e));
		statusled_set(ledmode_pulse_seldom);
		return "Failed.";
	}
	log_dbug(TAG,"verify=OK");
	if (result) {
		statusled_set(ledmode_pulse_seldom);
		return "Failed.";
	}
	if (changeboot && (bootp != updatep)) {
		int err = esp_ota_set_boot_partition(updatep);
		if (err != ESP_OK) {
			t.printf("set boot failed: %s\n",esp_err_to_name(err));
			statusled_set(ledmode_pulse_seldom);
			return "Failed.";
		}
	}
	statusled_set(ledmode_off);
	return 0;
}


const char *boot(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return "Invalid number of arguments.";
	if (argc == 1) {
		const esp_partition_t *b = esp_ota_get_boot_partition();
		const esp_partition_t *r = esp_ota_get_running_partition();
		const esp_partition_t *u = esp_ota_get_next_update_partition(NULL);
		term.printf("boot  : %s\n"
				"run   : %s\n"
				"update: %s\n"
				, b ? b->label : "<error>"
				, r ? r->label : "<error>"
				, u ? u->label : "<error>"
			   );
		return b && r ? 0 : "Failed.";
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
	printf("cannot boot %s\n",args[1]);
	return "Failed.";
}

#endif
