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
#include "romfs.h"
#include "support.h"
#include "terminal.h"
#include "wifi.h"

#include <esp_system.h>
#include <esp_ota_ops.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#ifdef ESP32
#include <mdns.h>
#include <mbedtls/base64.h>
#elif defined ESP8266
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

#define OTABUF_SIZE (4096)



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
	if (si.sin_addr.s_addr == IPADDR_NONE) {
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
#ifdef ESP8266
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
	t.printf("GET request submitted\n");
	return hsock;
}


static bool to_fd(Terminal &t, void *arg, char *buf, size_t s)
{
	//t.printf("to_fd(): %u bytes\n",s);
	if (-1 != write((int)arg,buf,s))
		return true;
	t.printf("write error: %s\n",strerror(errno));
	return false;
}


static bool to_ota(Terminal &t, void *arg, char *buf, size_t s)
{
	//t.printf("to_ota(): %u bytes\n",s);
	esp_err_t err = esp_ota_write((esp_ota_handle_t)arg,buf,s);
	if (err == ESP_OK)
		return true;
	t.printf("OTA flash failed: %s\n",esp_err_to_name(err));
	return false;
}


static bool socket_to_x(Terminal &t, int hsock, bool (*sink)(Terminal&,void*,char*,size_t), void *arg)
{
	char *buf = (char*)malloc(OTABUF_SIZE), *data;
	if (buf == 0) {
		t.printf("out of memory\n");
		return false;
	}
	bool ret = false;
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
		t.printf("unable to find end of header\n");
		goto done;
	}
	r -= (data-buf);
	while (r > 0) {
		numD += r;
		if (!sink(t,arg,data,r))
			goto done;
		r = recv(hsock,buf,OTABUF_SIZE,0);
		data = buf;
		t.printf("wrote %d bytes\r",numD);
	}
	t.printf("\n");
	ret = true;
done:
	free(buf);
	return ret;
}


static bool file_to_flash(Terminal &t, int fd, esp_ota_handle_t ota)
{
	size_t numD = 0;
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0) {
		t.printf("out of memory\n");
		return false;
	}
	bool r = false;
	for (;;) {
		int n = read(fd,buf,OTABUF_SIZE);
		if (n < 0) {
			t.printf("OTA read error: %s\n",strerror(errno));
			break;
		}
		if (n == 0) {
			//t.printf( "ota received %d bytes, wrote %d bytes",numD,numU);
			r = true;
			break;
		}
		numD += n;
		esp_err_t err = esp_ota_write(ota,buf,n);
		if (err != ESP_OK) {
			t.printf("ota write failed: %s\n",esp_err_to_name(err));
			break;
		}
		t.printf("wrote %d bytes\r",r);
	}
	t.printf("\n");
	free(buf);
	return r;
}


static bool http_to(Terminal &t, char *addr, bool (*sink)(Terminal &,void*,char*,size_t), void *arg)
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
		return false;
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
		t.printf("no file in http address\n");
		return false;
	}
	*filepath = 0;
	++filepath;
	char *portstr = strchr(server,':');
	if (portstr) {
		long l = strtol(++portstr,0,0);
		if ((l < 0) || (l > UINT16_MAX)) {
			t.printf("port value out of range\n");
			return false;
		}
		port = l;
	}
	int hsock = send_http_get(t,server,port,filepath,username);
	if (hsock == -1)
		return false;
	bool r = socket_to_x(t,hsock,sink,arg);
	close(hsock);
	return r;
}


bool http_download(Terminal &t, char *addr, const char *fn)
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
	bool r = http_to(t,addr,to_fd,(void*)fd);
	close(fd);
	return r;
}


#ifdef CONFIG_ROMFS
static bool to_romfs(Terminal &t, void *arg, char *buf, size_t s)
{
	uint32_t *addr = (uint32_t *)arg;
	//t.printf("to_romfs %u@%x\n",s,*addr);
	esp_err_t e = spi_flash_write(*addr,buf,s);
	if (e) {
		t.printf("flash error %s\n",esp_err_to_name(e));
		return false;
	}
	*addr += s;
	return true;

}


int update_romfs(Terminal &t, char *source)
{
	uint32_t addr = RomfsBaseAddr;
	t.printf("erasing flash\n");
	esp_err_t e = spi_flash_erase_range(RomfsBaseAddr,RomfsSpace);
	if (e)
		t.printf("warning: erasing flash failed\n");
	t.printf("programming flash\n");
	return http_to(t,source,to_romfs,(void*)&addr) ? 0 : 1;
}
#endif


int perform_ota(Terminal &t, char *source, bool changeboot)
{
	const esp_partition_t *bootp = esp_ota_get_boot_partition();
	const esp_partition_t *runp = esp_ota_get_running_partition();
	t.printf("running on '%s' at 0x%08x\n"
		,runp->label
		,runp->address);
	if (bootp != runp)
		t.printf("boot partition: '%s' at 0x%08x\n",bootp->label,bootp->address);
	const esp_partition_t *updatep = esp_ota_get_next_update_partition(NULL);
	if (updatep == 0) {
		t.printf("no OTA partition\n");
		return 1;
	}
	t.printf("erasing '%s' at 0x%x\n",updatep->label,updatep->address);
	esp_ota_handle_t ota = 0;
	esp_err_t err = esp_ota_begin(updatep, OTA_SIZE_UNKNOWN, &ota);
	if (err != ESP_OK) {
		t.printf("esp_ota_begin failed: %s\n",esp_err_to_name(err));
		return 1;
	}

	bool result;
	if ((0 == memcmp(source,"http://",7)) || (0 == memcmp(source,"https://",8))) {
		result = http_to(t,source,to_ota,(void*)ota);
	} else {
		int fd = open(source,O_RDONLY);
		if (fd == -1) {
			t.printf("open(%s): %s\n",source,strerror(errno));
			return 1;
		}
		t.printf("doing flash update\n");
		result = file_to_flash(t,fd,ota);
		close(fd);
	}

	if (esp_ota_end(ota) != ESP_OK) {
		t.printf("esp_ota_end failed\n");
		return 1;
	}
	if (!result)
		return 1;
	if (changeboot && (bootp != updatep)) {
		int err = esp_ota_set_boot_partition(updatep);
		if (err != ESP_OK) {
			t.printf("set boot failed: %s\n",esp_err_to_name(err));
			return 1;
		}
	}
	return 0;
}

#endif
