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

// RFC2347, RFC2348

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
#include <lwip/err.h>
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <lwip/ip6.h>
#endif
#include <mbedtls/base64.h>

#include <lwip/dns.h>
#include <lwip/udp.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TFTP_PORT 	69
#define TFTP_RRQ	1
#define TFTP_WRQ	2
#define TFTP_DATA	3
#define TFTP_ACK	4
#define TFTP_ERROR	5
#define TFTP_OACK	6

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
		t.printf("unterminated header: %d bytes\n",len);
		return 0;
	}
	data += 4;
	data[-1] = 0;
	if (strstr(text,"200 OK\r\n"))
		return data;
	t.printf("http error: %s\n",text);
	return 0;
}


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


static const char *socket_to_x(Terminal &t, LwTcp &P, const char *(*sink)(Terminal&,void*,char*,size_t), void *arg)
{
	char *buf = (char*)malloc(OTABUF_SIZE), *data;
	if (buf == 0)
		return "Out of memory.";
	const char *ret = "Failed.";
	size_t numD = 0, contlen = 0;
	bool ia = t.isInteractive();
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
		ret = "No body.";
		goto done;
	}
	r -= (data-buf);
	if (r == 0)
		goto read_data;
	while (r > 0) {
		if (const char *e = sink(t,arg,data,r)) {
			ret = e;
			goto done;
		}
		numD += r;
		if (ia) {
			t.printf("\r%d/%u written",numD,contlen);
			t.sync(false);
		}
		if (numD == contlen)
			break;
read_data:
		r = P.read(buf,OTABUF_SIZE,60000);
		data = buf;
	}
	if (r < 0) {
		ret = P.error();
	} else if (numD != contlen) {
		ret = "incomplete";
	} else {
		ret = 0;
	}
done:
	free(buf);
	return ret;
}


#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
typedef struct recv_arg
{
	const char *(*sink)(Terminal &,void*,char*,size_t);
	void *arg;
	char *buf;
	size_t size;
	uint16_t port;
	SemaphoreHandle_t sem;
} tftp_recv_arg_t;


static void tftp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *ip, u16_t port)
{
	tftp_recv_arg_t *r = (tftp_recv_arg_t *)arg;
	r->port = port;
	if (p->tot_len <= r->size+4) {
		r->size = p->tot_len;
		pbuf_copy_partial(p,r->buf,p->tot_len,0);
//		log_hex(TAG,r->buf,16,"packet %u",r->size);
	} else {
		log_warn(TAG,"packet too big");
	}
	xSemaphoreGive(r->sem);
	pbuf_free(p);
}


static const char *tftp_to(Terminal &t, uri_t *uri, const char *(*sink)(Terminal &,void*,char*,size_t), void *arg)
{
	struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
	udp_bind(pcb,IP_ANY_TYPE,0);
	size_t fl = strlen(uri->file);
	char buf[128];
	uint16_t reqsize = 1432;
	char *at = buf;
	*at++ = 0;
	*at++ = TFTP_RRQ;
	memcpy(at,uri->file,fl+1);
	at += fl+1;
	memcpy(at,"octet",6);
	at += 6;
	memcpy(at,"blksize",8);
	at += 8;
	at += sprintf(at,"%u",reqsize);
	++at;	// \0
	memcpy(at,"tsize\0000",8);
	at += 8;
	int n = at-buf;
	assert(n < sizeof(buf));
	log_hex(TAG,buf,n,"send:");
	struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT,n,PBUF_RAM);
	pbuf_take(pb,buf,n);
	tftp_recv_arg_t x;
	x.buf = (char*)malloc(reqsize+4);
	x.sem = xSemaphoreCreateBinary();
	x.size = reqsize;
	uint16_t blocksize = 512;
	udp_recv(pcb,tftp_recv,&x);
	err_t e = udp_sendto(pcb,pb,&uri->ip,uri->port);
	uint16_t seqid = 1;
	const char *r = 0;
	uint32_t total = 0, numexp = 0;
	pbuf_free(pb);
	buf[1] = TFTP_ACK;
	bool ia = t.isInteractive();
	do {
		if (pdTRUE != xSemaphoreTake(x.sem,10000)) {
			r = "Timeout.";
			break;
		}
		if (x.buf[0]) {
			r = "Packet error.";
			break;
		}
		if (TFTP_OACK == x.buf[1]) {
			log_hex(TAG,x.buf,x.size,"OACK");
			bool error = false;
			const char *blksize = (const char *)memmem(x.buf+2,x.size-2,"blksize",8);
			if (blksize && ((blksize[-1] == 0) || (blksize-x.buf == 2))) {
				long l = strtol(blksize+8,0,0);
				if ((l >= 8) && (l <= reqsize)) {
					blocksize = l;
					log_dbug(TAG,"blksize=%ld",l);
				} else {
					log_dbug(TAG,"invalid blksize=%ld",l);
					error = true;
				}
			}
			const char *tsize = (const char *)memmem(x.buf+2,x.size-2,"tsize",6);
			if (tsize && ((tsize-x.buf == 2) || (tsize[-1] == 0))) {
				long l = strtol(tsize+6,0,0);
				log_dbug(TAG,"tsize=%ld",l);
				numexp = l;
			}
			uint8_t send;
			if (error) {
				log_dbug(TAG,"invalid option");
				x.buf[1] = TFTP_ERROR;
				x.size = 0;
				send = 2;
			} else {
				x.buf[1] = TFTP_ACK;
				x.buf[2] = 0;
				x.buf[3] = 0;
				x.size = blocksize+4;
				send = 4;
			}
			pb = pbuf_alloc(PBUF_TRANSPORT,send,PBUF_RAM);
			pbuf_take(pb,x.buf,4);
			log_hex(TAG,x.buf,send,"send");
			e = udp_sendto(pcb,pb,&uri->ip,x.port);
			pbuf_free(pb);
		} else if (x.buf[1] == TFTP_DATA) {
			uint16_t id = (x.buf[2] << 8) | x.buf[3];
//			t.printf("id %d, size %u",id,x.size);
			if (id > seqid) {
				r = "Packet dropped.";
				break;
			}
			if (id == seqid) {
				log_dbug(TAG,"DATA id %d, size %u, blocksize %u",id,x.size,blocksize);
				r = sink(t,arg,x.buf+4,x.size-4);
				total += x.size-4;
				if (ia) {
					if (numexp)
						t.printf("\r%u/%u written",total,numexp);
					else
						t.printf("\r%u written",total);
					t.sync();
				}
				++seqid;
				x.buf[1] = TFTP_ACK;
				pb = pbuf_alloc(PBUF_TRANSPORT,4,PBUF_RAM);
				pbuf_take(pb,x.buf,4);
				e = udp_sendto(pcb,pb,&uri->ip,x.port);
				pbuf_free(pb);
			} else {
				log_dbug(TAG,"expected %d, got %d",seqid,id);
				e = 0;
			}
		} else if (x.buf[1] == TFTP_ERROR) {
			r = "Server returned an error.";
			break;
		}
//		log_dbug(TAG,"sentto %s:%u",ip2str_r(&ip,ipstr,sizeof(ipstr)),x.port);
		if (e)
			r = strlwiperr(e);
	} while ((r == 0) && ((x.size-4) == blocksize));
	udp_remove(pcb);
	free(x.buf);
	vSemaphoreDelete(x.sem);
	return r;
}


static const char *ftp_to(Terminal &t, uri_t *uri, const char *(*sink)(Terminal &,void*,char*,size_t), void *arg)
{
	if (uri->user == 0) {
		uri->user = "ftp";
		if (uri->pass == 0)
			uri->pass = "ftp";
	}
	t.printf("host=%s, user=%s, pass=%s, port=%u\n",uri->host,uri->user,uri->pass?uri->pass:"",uri->port);
	LwTcp P;
	char tmp[128];
	ipaddr_ntoa_r(&uri->ip,tmp,sizeof(tmp));
	if (P.connect(&uri->ip,uri->port))
		return P.error();
	int n = P.read(tmp,sizeof(tmp),5000/portTICK_PERIOD_MS);
	if (n < 0)
    		return P.error();
	if ((n < 4) || (0 != strncmp(tmp,"220 ",4)))
		return "Protocol error.";
	size_t ul = strlen(uri->user);
	if (ul > 122)
		return "Invalid argument.";
	memcpy(tmp,"USER ",5);
	memcpy(tmp+5,uri->user,ul);
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
	if (uri->pass) {
		size_t pl = strlen(uri->pass);
		if (pl > 122)
			return "Invalid argument.";
		memcpy(tmp,"PASS ",5);
		memcpy(tmp+5,uri->pass,pl);
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
	res = P.write("TYPE I\r\n",8);
	if (res < 0) {
		t.printf("send: %s",P.error());
		return "";
	}
	n = P.read(tmp,sizeof(tmp));
	res = P.write("PASV\r\n",6);
	if (res < 0) {
		t.printf("send PASV: %s",P.error());
		return "";
	}
	log_dbug(TAG,"send 'PASV'");
	n = P.read(tmp,sizeof(tmp),5000/portTICK_PERIOD_MS);
	if (n < 0) {
		t.printf("send PASV: %s",P.error());
		return "";
	}
	log_dbug(TAG,"rcvd '%.*s'",n,tmp);
	unsigned pasv[6];
	n = sscanf(tmp,"227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)."
			,pasv+0,pasv+1,pasv+2,pasv+3,pasv+4,pasv+5);
//	t.printf("%d: %d,%d,%d,%d,%d,%d\n",n,pasv[0],pasv[1],pasv[2],pasv[3],pasv[4],pasv[5]);
	if (n != 6)
		return "Connection failed.";
	sprintf(tmp,"%u.%u.%u.%u",pasv[0],pasv[1],pasv[2],pasv[3]);
	uint16_t dp = pasv[4] << 8 | pasv[5];
	ip_addr_t dip;
	if (0 == ipaddr_aton(tmp,&dip))
		return "Address error.";
	memcpy(tmp,"RETR ",5);
	size_t fl = strlen(uri->file);
	memcpy(tmp+5,uri->file,fl);
	tmp[fl+5] = '\r';
	tmp[fl+6] = '\n';
	tmp[fl+7] = 0;
	res = P.write(tmp,fl+7);
	if (res < 0) {
		t.printf("send RETR: %s",P.error());
		return "";
	}
	LwTcp D;
	if (D.connect(&dip,dp,false)) {
		t.printf("connect to port %u: %s\n",dp,D.error());
		return "";
	}
	n = P.read(tmp,sizeof(tmp),5000);
	if (n < 0) {
		t.printf("read RETR: %s",P.error());
		return "";
	}
	if ((tmp[0] != '2') && (tmp[0] != '1'))
		return "Server error.";
	unsigned total = 0;
	if (const char *b = strchr(tmp,'(')) {
		++b;
		total = strtol(b,0,0);
	}
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0)
		return "Out of memory.";
	const char *ret = 0;
	unsigned written = 0;
	bool ia = t.isInteractive();
	n = D.read(buf,OTABUF_SIZE);
	while (n > 0) {
		written += n;
		if (ia) {
			t.printf("\r%d/%u written",written,total);
			t.sync();
		}
		if (sink(t,arg,buf,n)) {
			ret = "";
			break;
		}
		n = D.read(buf,OTABUF_SIZE);
	}
	if (n < 0)
		ret = D.error();
	P.write("QUIT\r\n",6);
	free(buf);
	return ret;
}


static const char *file_to(Terminal &t, uri_t *uri, const char *(*sink)(Terminal &,void*,char*,size_t), void *arg)
{
	t.printf("open file %s\n",uri->file);
	int fd = open(uri->file,O_RDONLY);
	if (fd == -1) {
		t.printf("open %s\n",uri->file);
		return strerror(errno);
	}
	char *buf = (char*)malloc(OTABUF_SIZE);
	if (buf == 0) {
		close(fd);
		return "Out of memory.";
	}
	bool ia = t.isInteractive();
	size_t numD = 0;
	const char *r = 0;
	do {
		if (ia) {
			t.printf("\r%d written",numD);
			t.sync();
		}
		int n = read(fd,buf,OTABUF_SIZE);
		if (n < 0) {
			t.printf("OTA read error: %s\n",strerror(errno));
			r = "";
			break;
		}
		if (n == 0) {
			r = 0;
			break;
		}
		numD += n;
		r = sink(t,arg,buf,n);
	} while (r == 0);
	if (ia)
		t.println();
	free(buf);
	close(fd);
	return r;
}
#endif	// !CONFIG_ESPTOOLPY_FLASHSIZE_1MB


static const char *http_to(Terminal &t, uri_t *uri, const char *(*sink)(Terminal &,void*,char*,size_t), void *arg)
{
	t.printf("host %s, port %u, file %s\n",uri->host,uri->port,uri->file);
	LwTcp P;
	P.connect(&uri->ip,uri->port);
	const char *r = send_http_get(t,P,uri->host,uri->port,uri->file,uri->user);
	if (r == 0)
		r = socket_to_x(t,P,sink,arg);
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
	uri_t uri;
	const char *r = uri_parse(addr,&uri);
	if (r == 0)
		r = http_to(t,&uri,to_fd,(void*)fd);
	close(fd);
	return r;
}


static const char *to_part(Terminal &t, void *arg, char *buf, size_t s)
{
	esp_partition_t *p = (esp_partition_t *)arg;
	//t.printf("to_part %u@%x\n",s,*addr);
	if (s > p->size)
		return "End of partition.";
#if IDF_VERSION >= 50
	// TODO FIXME moving partiton address in to_part might not be a
	// good idea
	esp_err_t e = esp_partition_write_raw(p,0,buf,s);
#else
	esp_err_t e = spi_flash_write(p->address,buf,s);
#endif
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
	uri_t uri;
	if (const char *r = uri_parse(source,&uri))
		return r;
	uint32_t addr = p->address;
	uint32_t s = p->size;
	t.printf("erasing %d@%x\n",p->size,p->address);
	t.sync();
#if IDF_VERSION >= 50
	if (esp_err_t e = esp_partition_erase_range(p,0,p->size))
		return esp_err_to_name(e);
#else
	if (esp_err_t e = spi_flash_erase_range(p->address,p->size))
		return esp_err_to_name(e);
#endif
	const char *r;
	switch (uri.prot) {
	case prot_http:
		r = http_to(t,&uri,to_part,(void*)p);
		break;
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	case prot_file:
		r = file_to(t,&uri,to_part,(void*)p);
		break;
	case prot_ftp:
		r = ftp_to(t,&uri,to_part,(void*)p);
		break;
	case prot_tftp:
		r = tftp_to(t,&uri,to_part,(void*)p);
		break;
#endif
	default:
		r = "Invalid protocol";
	}
	bool ia = t.isInteractive();
	if (ia)
		t.println();
	p->address = addr;
	p->size = s;
	return r;
}


const char *perform_ota(Terminal &t, char *src, bool changeboot)
{
	uri_t uri;
	if (const char *r = uri_parse(src,&uri))
		return r;
	char ipstr[64];
	ip2str_r(&uri.ip,ipstr,sizeof(ipstr));
	t.printf("contacting %s\n",ipstr);
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
		statusled_set(ledmode_off);
		return "no OTA partition";
	}
	if (ia) {
		t.printf("erasing '%s' at 0x%08x\n",updatep->label,updatep->address);
		t.sync();
	}
	if (esp_err_t err = esp_ota_begin(updatep, OTA_SIZE_UNKNOWN, &ota)) {
		t.printf("OTA begin: %s\n",esp_err_to_name(err));
		statusled_set(ledmode_fast);
		return "";
	}
	switch (uri.prot) {
	default:
		result = "Unsupported protocol.";
		break;
	case prot_http:
		result = http_to(t,&uri,to_ota,(void*)ota);
		break;
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	case prot_file:
		result = file_to(t,&uri,to_ota,(void*)ota);
		break;
	case prot_ftp:
		result = ftp_to(t,&uri,to_ota,(void*)ota);
		break;
	case prot_tftp:
		result = tftp_to(t,&uri,to_ota,(void*)ota);
		break;
#endif
	}
	ledmode_t mode = ledmode_fast;
	if (ia)
		t.println();
	if (result == 0) {
		if (ia)
			t.println("verify");
		if (esp_err_t e = esp_ota_end(ota)) {
			log_warn(TAG,"esp_ota_end: %s\n",esp_err_to_name(e));
			result = esp_err_to_name(e);
		} else if (changeboot && (bootp != updatep)) {
			if (int err = esp_ota_set_boot_partition(updatep)) {
				t.printf("set boot failed: %s\n",esp_err_to_name(err));
				result = "";
			} else {
				mode = ledmode_off;
			}
		} else {
			mode = ledmode_off;
		}
	}
	statusled_set(mode);
	return result;
}


const char *ota_from_server(Terminal &term, const char *server, const char *version)
{
	if (server[0] == 0)
		return "OTA server not set";
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
