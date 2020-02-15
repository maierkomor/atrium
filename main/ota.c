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

#include "log.h"
#include "ota.h"
#include "shell.h"
#include "romfs.h"
#include "support.h"
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
#include <esp_base64.h>
#include <lwip/ip_addr.h>
#include <lwip/ip6.h>
#include <lwip/err.h>
#include <lwip/apps/mdns.h>
#endif

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

#define OTABUF_SIZE (4096*2)

static const char TAG[] = "OTA", CrNl[] = "\r\n";



static char *skip_http_header(char *text, int len)
{
	char *data = strstr(text,"\r\n\r\n");
	if (data == 0) {
		log_error(TAG,"unterminated http header: %d bytes\n",len);
		return 0;
	}
	data += 4;
	data[-1] = 0;
	//log_dbug(TAG,"header:\n%s",text);
	if (strstr(text,"200 OK\r\n"))
		return data;
	log_error(TAG,"http server returned error");
	return 0;
}


static int send_http_get(const char *server, int port, const char *filename, const char *auth)
{
	log_info(TAG, "http get: host=%s, port=%d, filename=%s", server, port, filename);
	struct sockaddr_in si;
	memset(&si, 0, sizeof(struct sockaddr_in));
	si.sin_family = AF_INET;
	si.sin_port = htons(port);
	si.sin_addr.s_addr = resolve_hostname(server);
	if (si.sin_addr.s_addr == IPADDR_NONE) {
		log_error(TAG,"could not resolve hostname %s",server);
		return -1;
	}
	log_info(TAG,"connecting to %d.%d.%d.%d"
			,si.sin_addr.s_addr&0xff
			,(si.sin_addr.s_addr>>8)&0xff
			,(si.sin_addr.s_addr>>16)&0xff
			,(si.sin_addr.s_addr>>24)&0xff
			);

	int hsock = socket(AF_INET, SOCK_STREAM, 0);
	if (hsock == -1) {
		log_error(TAG, "unable to create download socket: %s",strerror(errno));
		return -1;
	}
	if (-1 == connect(hsock, (struct sockaddr *)&si, sizeof(si))) {
		log_error(TAG,"connect to %s failed: %s",server,strerror(errno));
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
		log_info(TAG,"%d",sizeof(http_request)-get_len);
		int n = esp_base64_encode(auth,strlen(auth),http_request+get_len,sizeof(http_request)-get_len);
		if (n < 0) {
			log_error(TAG,"base64: %d",n);
			return -1;
		}
		get_len += n;
#else
		size_t b64l = sizeof(http_request)-get_len;
		int n = mbedtls_base64_encode((unsigned char *)http_request+get_len,sizeof(http_request)-get_len,&b64l,(unsigned char *)auth,strlen(auth));
		if (0 > n) {
			log_error(TAG,"base64: %d",n);
			return -1;
		}
		get_len += b64l;
#endif
		strcpy(http_request+get_len,CrNl);
		get_len += 2;
	}
	if (get_len + 3 > sizeof(http_request))
		return -1;
	strcpy(http_request+get_len,CrNl);
	get_len += 2;
	//log_dbug(TAG,"http request:\n'%s'",http_request);
	int res = send(hsock, http_request, get_len, 0);
	if (res < 0) {
		log_error(TAG, "unable to send get request: %s",strerror(errno));
    		return -1;
	}
	log_info(TAG, "GET request submitted");
	return hsock;
}


static bool to_fd(void *arg, char *buf, size_t s)
{
	//log_dbug(TAG,"to_fd(): %u bytes",s);
	if (-1 != write((int)arg,buf,s))
		return true;
	log_error(TAG, "write error: %s",strerror(errno));
	return false;
}


static bool to_ota(void *arg, char *buf, size_t s)
{
	//log_dbug(TAG,"to_ota(): %u bytes",s);
	esp_err_t err = esp_ota_write((esp_ota_handle_t)arg,buf,s);
	if (err == ESP_OK)
		return true;
	log_error(TAG, "OTA flash failed: %s",esp_err_to_name(err));
	return false;
}


static bool socket_to_x(int hsock, bool (*sink)(void*,char*,size_t), void *arg)
{
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0) {
		log_error(TAG,"out of memory");
		return false;
	}
	int r = recv(hsock,buf,OTABUF_SIZE,0);
	if (r < 0) {
		free(buf);
		return false;
	}
	if (memcmp(buf,"HTTP",4) || memcmp(buf+8," 200 OK\r",8)) {
		char *nl = strchr(buf,'\n');
		if (nl) {
			*nl = 0;
			log_error(TAG,"unexpted server answer: %s",buf);
		}
		free(buf);
		return false;
	}
	char *data = skip_http_header(buf,r);
	if (data == 0) {
		log_error(TAG,"unable to find end of header");
		free(buf);
		return false;
	}
	if (!sink(arg,data,r-(data-buf))) {
		free(buf);
		return false;
	}
	size_t numD = r-(data-buf);
	size_t numW = numD;
	r = recv(hsock,buf,OTABUF_SIZE,0);
	while (r > 0) {
		numD += r;
		if (!sink(arg,buf,r)) {
			free(buf);
			return false;
		}
		numW += r;
		r = recv(hsock,buf,OTABUF_SIZE,0);
	}
	free(buf);
	log_info(TAG,"received %d bytes, wrote %d bytes",numD,numW);
	return true;
}


static bool file_to_flash(int fd, esp_ota_handle_t ota)
{
	size_t numD = 0;
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0) {
		log_error(TAG,"not enough RAM to perform update");
		return false;
	}
	for (;;) {
		int r = read(fd,buf,OTABUF_SIZE);
		if (r < 0) {
			log_error(TAG, "OTA read error: %s",strerror(errno));
			free(buf);
			return false;
		}
		if (r == 0) {
			//log_dbug(TAG, "ota received %d bytes, wrote %d bytes",numD,numU);
			free(buf);
			return true;
		}
		numD += r;
		esp_err_t err = esp_ota_write(ota,buf,r);
		if (err != ESP_OK) {
			log_error(TAG, "ota write failed: %s",esp_err_to_name(err));
			free(buf);
			return false;
		}
		log_info(TAG,"%d bytes flashed",r);
	}
}


static bool http_to(char *addr, bool (*sink)(void*,char*,size_t), void *arg)
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
		log_error(TAG,"http_to('%s'): invalid address",addr);
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
		log_error(TAG,"no file in http address");
		return false;
	}
	*filepath = 0;
	++filepath;
	char *portstr = strchr(server,':');
	if (portstr) {
		long l = strtol(++portstr,0,0);
		if ((l < 0) || (l > UINT16_MAX)) {
			log_error(TAG,"port value out of range");
			return false;
		}
		port = l;
	}
	int hsock = send_http_get(server,port,filepath,username);
	if (hsock == -1)
		return false;
	bool r = socket_to_x(hsock,sink,arg);
	close(hsock);
	return r;
}


bool http_download(char *addr, const char *fn)
{
	if (fn == 0) {
		 char *sl = strrchr(addr,'/');
		 if (sl == 0) {
			 log_info(TAG,"invalid address '%s'",addr);
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
		log_error(TAG,"error creating %s: %s",fn,strerror(errno));
		return false;
	}
	log_info(TAG,"downloading to %s",fn);
	bool r = http_to(addr,to_fd,(void*)fd);
	close(fd);
	return r;
}


#ifdef CONFIG_ROMFS
static bool to_romfs(void *arg, char *buf, size_t s)
{
	uint32_t *addr = (uint32_t *)arg;
	//log_info(TAG,"to_romfs %u@%x",s,*addr);
	esp_err_t e = spi_flash_write(*addr,buf,s);
	if (e) {
		log_error(TAG, "flash error %s",esp_err_to_name(e));
		return false;
	}
	*addr += s;
	return true;

}


int update_romfs(char *source)
{
	uint32_t addr = RomfsBaseAddr;
	log_info(TAG,"erasing flash");
	esp_err_t e = spi_flash_erase_range(RomfsBaseAddr,RomfsSpace);
	if (e)
		log_warn(TAG,"erasing flash failed");
	log_info(TAG,"programming flash");
	return http_to(source,to_romfs,(void*)&addr) ? 0 : 1;
}
#endif


int perform_ota(char *source, bool changeboot)
{
	const esp_partition_t *bootp = esp_ota_get_boot_partition();
	const esp_partition_t *runp = esp_ota_get_running_partition();
	log_info(TAG,"running on '%s' at 0x%08x"
		,runp->label
		,runp->address);
	if (bootp != runp)
		log_warn(TAG, "boot partition: '%s' at 0x%08x",bootp->label,bootp->address);
	const esp_partition_t *updatep = esp_ota_get_next_update_partition(NULL);
	if (updatep == 0) {
		log_error(TAG,"no OTA partition");
		return 1;
	}
	log_info(TAG, "updating '%s' at 0x%x",updatep->label,updatep->address);
	esp_ota_handle_t ota = 0;
	esp_err_t err = esp_ota_begin(updatep, OTA_SIZE_UNKNOWN, &ota);
	if (err != ESP_OK) {
		log_error(TAG, "esp_ota_begin failed: %s",esp_err_to_name(err));
		return 1;
	}

	bool result;
	if ((0 == memcmp(source,"http://",7)) || (0 == memcmp(source,"https://",8))) {
		result = http_to(source,to_ota,(void*)ota);
	} else {
		int fd = open(source,O_RDONLY);
		if (fd == -1) {
			log_error(TAG,"open(%s): %s",source,strerror(errno));
			return 1;
		}
		log_info(TAG, "doing flash update");
		result = file_to_flash(fd,ota);
		close(fd);
	}

	if (esp_ota_end(ota) != ESP_OK) {
		log_error(TAG, "esp_ota_end failed");
		return 1;
	}
	if (!result)
		return 1;
	if (changeboot && (bootp != updatep)) {
		int err = esp_ota_set_boot_partition(updatep);
		if (err != ESP_OK) {
			log_error(TAG,"set boot failed: %s\n",esp_err_to_name(err));
			return 1;
		}
	}
	return 0;
}

#endif
