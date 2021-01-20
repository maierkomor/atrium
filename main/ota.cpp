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

#ifdef CONFIG_OTA

#include "ota.h"
#include "shell.h"
#include "status.h"
#include "support.h"
#include "terminal.h"
#include "wifi.h"

#include <esp_system.h>
#include <esp_ota_ops.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#ifdef CONFIG_IDF_TARGET_ESP32
#include <mdns.h>
#include <mbedtls/base64.h>
#elif defined CONFIG_IDF_TARGET_ESP8266
extern "C" {
#include <esp_base64.h>
}
#include <lwip/ip_addr.h>
#include <lwip/ip6.h>
#include <lwip/err.h>
#include <lwip/apps/mdns.h>
#endif
#include <mbedtls/base64.h>

#include <lwip/dns.h>

#ifdef CONFIG_MDNS
#include <mdns.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define OTABUF_SIZE 4096


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


static int send_http_get(Terminal &t, const char *server, int port, const char *filename, const char *auth)
{
	t.printf("http get: host=%s, port=%d, filename=%s\n", server, port, filename);
	struct sockaddr_in si;
	memset(&si, 0, sizeof(struct sockaddr_in));
	si.sin_family = AF_INET;
	si.sin_port = htons(port);
	si.sin_addr.s_addr = resolve_hostname(server);
	if ((si.sin_addr.s_addr == IPADDR_NONE) || (si.sin_addr.s_addr == 0)) {
		t.printf("could not resolve hostname %s\n",server);
		return -1;
	}
	t.printf("connecting to %d.%d.%d.%d\n"
			,si.sin_addr.s_addr&0xff
			,(si.sin_addr.s_addr>>8)&0xff
			,(si.sin_addr.s_addr>>16)&0xff
			,(si.sin_addr.s_addr>>24)&0xff
			);

	int hsock = socket(AF_INET, SOCK_STREAM, 0);
	if (hsock == -1) {
		t.printf("unable to create download socket: %s\n",strerror(errno));
		return -1;
	}
	if (-1 == connect(hsock, (struct sockaddr *)&si, sizeof(si))) {
		t.printf("connect to %s failed: %s\n",server,strerror(errno));
		close(hsock);
		return -1;
	}
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
	t.println("sent GET");
	return hsock;
}


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
	//t.printf("to_ota(): %u bytes\n",s);
	esp_err_t err = esp_ota_write((esp_ota_handle_t)arg,buf,s);
	if (err == ESP_OK)
		return 0;
	t.printf("OTA flash failed: %s\n",esp_err_to_name(err));
	return 1;
}


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
			t.printf("unexpted server answer: %s\n",buf);
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


static int file_to_flash(Terminal &t, int fd, esp_ota_handle_t ota)
{
	size_t numD = 0;
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0) {
		t.println("out of memory");
		return false;
	}
	int r = 1;
	for (;;) {
		int n = read(fd,buf,OTABUF_SIZE);
		if (n < 0) {
			t.printf("OTA read error: %s\n",strerror(errno));
			break;
		}
		if (n == 0) {
			//t.printf( "ota received %d bytes, wrote %d bytes",numD,numU);
			r = 0;
			break;
		}
		numD += n;
		esp_err_t err = esp_ota_write(ota,buf,n);
		if (err != ESP_OK) {
			t.printf("ota write failed: %s\n",esp_err_to_name(err));
			break;
		}
		t.printf("\rwrote %d bytes",r);
	}
	t.println();
	free(buf);
	return r;
}


static int http_to(Terminal &t, char *addr, int (*sink)(Terminal &,void*,char*,size_t), void *arg)
{
	uint16_t port;
	const char *server;
	if (0 == strncmp(addr,"http://",7)) {
		port = 80;
		server = addr + 7;
	} else if (0 == strncmp(addr,"https://",8)) {
		port = 443;
		server = addr + 8;
	} else {
		t.printf("http_to('%s'): invalid address\n",addr);
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
	int hsock = send_http_get(t,server,port,filepath,username);
	if (hsock == -1)
		return 1;
	int r = socket_to_x(t,hsock,sink,arg);
	close(hsock);
	return r;
}


int http_download(Terminal &t, char *addr, const char *fn)
{
	if (fn == 0) {
		 char *sl = strrchr(addr,'/');
		 if (sl == 0) {
			 t.printf("invalid address '%s'\n",addr);
			 return false;
		 }
		 fn = sl+1;
	}
	int fd;
	if (fn[0] != '/') {
		const char *pwd = getpwd();
		char *path = (char*)malloc(strlen(pwd)+strlen(fn)+1);
		strcpy(path,pwd);
		strcat(path,fn);
		fd = creat(path,0666);
		free(path);
	} else {
		fd = creat(fn,0666);
	}
	if (fd == -1) {
		t.printf("error creating %s: %s\n",fn,strerror(errno));
		return false;
	}
	t.printf("downloading to %s\n",fn);
	int r = http_to(t,addr,to_fd,(void*)fd);
	close(fd);
	return r;
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


int update_part(Terminal &t, char *source, const char *dest)
{
	esp_partition_t *p = (esp_partition_t *) esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,dest);
	if (p == 0) {
		t.printf("unknown partition '%s'",dest);
		return 1;
	}
	const esp_partition_t *b = esp_ota_get_running_partition();
	if ((b->address <= p->address) && (b->address+b->size >= p->address)) {
		t.println("cannot update to running partition");
		return 1;
	}
	uint32_t addr = p->address;
	uint32_t s = p->size;
	t.printf("erasing %d@%x\n",p->size,p->address);
	if (esp_err_t e = spi_flash_erase_range(p->address,p->size)) {
		t.printf("error erasing: %s\n",esp_err_to_name(e));
		return 1;
	}
	int r = http_to(t,source,to_part,(void*)p);
	p->address = addr;
	p->size = s;
	return r;
}


int perform_ota(Terminal &t, char *source, bool changeboot)
{
	statusled_set(ledmode_pulse_often);
	const esp_partition_t *bootp = esp_ota_get_boot_partition();
	const esp_partition_t *runp = esp_ota_get_running_partition();
	t.printf("running on '%s' at 0x%08x\n"
		,runp->label
		,runp->address);
	if (bootp != runp)
		t.printf("boot partition: '%s' at 0x%08x\n",bootp->label,bootp->address);
	const esp_partition_t *updatep = esp_ota_get_next_update_partition(NULL);
	if (updatep == 0) {
		t.println("no OTA partition");
		statusled_set(ledmode_pulse_seldom);
		return 1;
	}
	t.printf("erasing '%s' at 0x%08x\n",updatep->label,updatep->address);
	esp_ota_handle_t ota = 0;
	esp_err_t err = esp_ota_begin(updatep, OTA_SIZE_UNKNOWN, &ota);
	if (err != ESP_OK) {
		t.printf("esp_ota_begin failed: %s\n",esp_err_to_name(err));
		statusled_set(ledmode_pulse_seldom);
		return 1;
	}

	int result;
	if ((0 == memcmp(source,"http://",7)) || (0 == memcmp(source,"https://",8))) {
		result = http_to(t,source,to_ota,(void*)ota);
	} else {
		t.printf("open file %s\n",source);
		int fd = open(source,O_RDONLY);
		if (fd == -1) {
			t.printf("open %s: %s\n",source,strerror(errno));
			return 1;
		}
		result = file_to_flash(t,fd,ota);
		close(fd);
	}
	if (esp_err_t e = esp_ota_end(ota)) {
		t.printf("esp_ota_end: %s\n",esp_err_to_name(e));
		statusled_set(ledmode_pulse_seldom);
		return 1;
	}
	if (result) {
		statusled_set(ledmode_pulse_seldom);
		return 1;
	}
	if (changeboot && (bootp != updatep)) {
		int err = esp_ota_set_boot_partition(updatep);
		if (err != ESP_OK) {
			t.printf("set boot failed: %s\n",esp_err_to_name(err));
			statusled_set(ledmode_pulse_seldom);
			return 1;
		}
	}
	statusled_set(ledmode_off);
	return 0;
}


int boot(Terminal &term, int argc, const char *args[])
{
	if (argc > 2)
		return arg_invnum(term);
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
		return b && r ? 0 : 1;
	}
	if (0 == term.getPrivLevel())
		return arg_priv(term);
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
	printf("cannot boot %s",args[1]);
	return 1;
}
#endif
