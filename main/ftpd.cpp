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

#ifdef CONFIG_FTP

#define FTP_ROOT "/"

#ifdef ESP8266
#define USE_FOPEN
#endif

#include "globals.h"
#include "inetd.h"
#include "log.h"
#include "netsvc.h"
#include "settings.h"
#include "shell.h"
#include "support.h"
#include "swcfg.h"
#include "tcp_terminal.h"
#include "wifi.h"

#include <lwip/tcp.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFSIZE (16*1024)


typedef struct ftpctx
{
	const char *root;
	char *wd;
	char *rnfr;
	int con;
	int dcon;
	uint8_t login;	// 2 = ftp, 4 = root, 1 = unlocked
} ftpctx_t;


typedef struct ftpcom
{
	const char cmd[6];
	void (*fun)(ftpctx_t *ctx, const char *arg);
} ftpcom_t;


static const char TAG[] = "ftpd";


static char *arg2fn(ftpctx_t *ctx, const char *arg)
{
	char *fn;
	if (arg[0] == '/') {
		fn = (char*)malloc(strlen(ctx->root)+strlen(arg)+1);
		strcpy(fn,ctx->root);
	} else {
		fn = (char*)malloc(strlen(ctx->root)+strlen(ctx->wd)+strlen(arg)+1);
		strcpy(fn,ctx->root);
		strcat(fn,ctx->wd);
	}
	strcat(fn,arg);
	return fn;
}


static void answer(ftpctx_t *ctx, const char *fmt, ...)
{
	char buf[80], *b = buf;
	va_list val;
	va_start(val,fmt);
	int n = vsnprintf(buf,sizeof(buf),fmt,val);
	va_end(val);
	if (n >= sizeof(buf)) {
		va_start(val,fmt);
		n = vasprintf(&b,fmt,val);
		va_end(val);
	}
	log_dbug(TAG,"answer %s",b);
	if (-1 == send(ctx->con,b,n,0)) {
		if (b != buf)
			free(b);
		log_error(TAG,"failed to send answer '%s': %s",b,strneterr(ctx->con));
		close(ctx->con);
		vTaskDelete(0);
	}
	if (b != buf)
		free(b);
}


static char *up_slash(char *str, char *at)
{
	while (str != at) {
		if (*at == '/')
			return at;
		--at;
	}
	return at;
}


static void fold_path(char *path)
{
	log_dbug(TAG,"folding %s",path);
	while (0 == memcmp(path,"/../",4))
		memmove(path,path+3,strlen(path+3)+1);
	char *dd = strstr(path,"/../");
	while (dd) {
		char *sl = up_slash(path,dd-1);
		if (sl) {
			memmove(sl+1,dd+4,strlen(dd+4)+1);
		} else {
			memmove(dd,dd+3,strlen(dd+3)+1);
		}
		log_dbug(TAG,"folded to %s",path);
		dd = strstr(path,"/../");
	}
}


static void pwd(ftpctx_t *ctx, const char *arg)
{
	answer(ctx,"200: %s\r\n",ctx->wd);
}


static void rnfr(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		// 501: syntax/argument error
		answer(ctx,"501 missing argument\r\n");
		return;
	}
	if (ctx->rnfr) {
		answer(ctx,"503 rename from already set\r\n");
		return;
	}
	char *fn = arg2fn(ctx,arg);
#ifdef USE_FOPEN
	FILE *f = fopen(fn,"r");
	if (f == 0) {
		free(fn);
		answer(ctx,"550 unable to stat file: %s\r\n",strerror(errno));
		return;
	}
	fclose(f);
#else
	struct stat st;
	if (-1 == stat(fn,&st)) {
		free(fn);
		answer(ctx,"550 unable to stat file: %s\r\n",strerror(errno));
		return;
	}
#endif
	ctx->rnfr = fn;
	answer(ctx,"350 ready to rename\r\n");
}


static void rnto(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		// 501: syntax/argument error
		answer(ctx,"501 missing argument\r\n");
		return;
	}
	if (ctx->rnfr == 0) {
		answer(ctx,"503 rename from not set\r\n");
		return;
	}
	char *fn = arg2fn(ctx,arg);
	int r = rename(ctx->rnfr,fn);
	free(fn);
	free(ctx->rnfr);
	ctx->rnfr = 0;
	if (r == 0)
		answer(ctx,"250 file renamed to %s\r\n",arg);
	else
		answer(ctx,"550 file rename failed:%s\r\n",strerror(errno));
}


static void mkd(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		// 501: syntax/argument error
		answer(ctx,"501 missing argument\r\n");
		return;
	}
	char buf[256];
	strcpy(buf,ctx->root);
	strcat(buf,ctx->wd);
	strcat(buf,arg);
	fold_path(buf);
#ifdef ESP8266
	answer(ctx,"452 operation not supported\r\n");
#else
	log_dbug(TAG,"mkdir %s",buf);
	if (-1 == mkdir(buf,0777)) {
		log_warn(TAG,"failed to create %s: %s",arg,strerror(errno));
		answer(ctx,"552 failed to create %s: %s\r\n",arg,strerror(errno));
		return;
	}
	answer(ctx,"200 OK\r\n");
#endif
}


static void noop(ftpctx_t *ctx, const char *arg)
{
	answer(ctx,"200 OK\r\n");
}


static void rmd(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		// 501: syntax/argument error
		answer(ctx,"501 missing argument\r\n");
		return;
	}
	char buf[256];
	strcpy(buf,ctx->root);
	strcat(buf,ctx->wd);
	strcat(buf,arg);
	fold_path(buf);
#ifdef ESP8266
	answer(ctx,"452 operation not supported\r\n");
#else
	log_dbug(TAG,"rmdir %s",buf);
	if (-1 == rmdir(buf)) {
		log_warn(TAG,"failed to rmdir %s: %s",arg,strerror(errno));
		answer(ctx,"552 failed to rmdir %s: %s\r\n",arg,strerror(errno));
		return;
	}
	answer(ctx,"200 OK\r\n");
#endif
}


static void retrive(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		answer(ctx,"501 missing argument\r\n");
		return;
	}
	if (ctx->dcon == -1) {
		answer(ctx,"501 PORT missing\r\n");
		log_warn(TAG,"store request without port");
		return;
	}
	char fn[256];
	strcpy(fn,ctx->root);
	strcat(fn,ctx->wd);
	strcat(fn,arg);
#ifdef USE_FOPEN
	FILE *f = fopen(fn,"r");
	if (f == 0) {
		answer(ctx,"552 unable to open %s: %s\r\n",arg,strerror(errno));
		log_warn(TAG,"unable to open %s: %s",fn,strerror(errno));
		return;
	}
	char *buf = (char*)malloc(BUFSIZE);
	if (buf == 0) {
		answer(ctx,"552 unable to allocate mem for reading\r\n");
		log_warn(TAG,"unable to allocate mem for reading\r\n");
		return;
	}
	answer(ctx,"150 opened %s\r\n",arg);
	int r, total = 0;
	do {
		r = fread(buf,1,BUFSIZE,f);
		if (r > 0) {
			int n = send(ctx->dcon,buf,r,0);
			if (-1 == n) {
				// 552: action aborted
				answer(ctx,"552 error sending: %s\r\n",strneterr(ctx->dcon));
				log_warn(TAG,"unable to retrive %s: %s",arg,strneterr(ctx->dcon));
				goto cleanup;
			}
			total += n;
		} else if (r < 0) {
			// 552: action aborted
			answer(ctx,"552 unable to read from %s: %s\r\n",arg,strerror(errno));
			log_warn(TAG,"unable to read from %s: %s",arg,strerror(errno));
			goto cleanup;
		}
		log_dbug(TAG,"sent %d bytes",r);
	} while (r > 0);
	log_dbug(TAG,"sent %d bytes",total);
	answer(ctx,"200\r\n");
cleanup:
	close(ctx->dcon);
	ctx->dcon = -1;
	free(buf);
	fclose(f);
#else
	int fd = open(fn,O_RDONLY,0);
	if (fd == -1) {
		answer(ctx,"552 unable to open %s: %s\r\n",arg,strerror(errno));
		log_warn(TAG,"unable to open %s: %s",fn,strerror(errno));
		return;
	}
	struct stat st;
	if (-1 == fstat(fd,&st)) {
		close(fd);
		answer(ctx,"552 unable to stat %s: %s\r\n",arg,strerror(errno));
		log_warn(TAG,"unable to open %s: %s",fn,strerror(errno));
		return;
	}
	char *buf = (char*)malloc(BUFSIZE);
	assert(buf);
	answer(ctx,"150 opened %s\r\n",arg);
	int r, total = 0;
	do {
		r = read(fd,buf,BUFSIZE);
		if (r > 0) {
			int n = send(ctx->dcon,buf,r,0);
			if (-1 == n) {
				// 552: action aborted
				answer(ctx,"552 error sending: %s\r\n",strneterr(ctx->dcon));
				log_warn(TAG,"unable to retrive %s: %s",arg,strneterr(ctx->dcon));
				goto cleanup;
			}
			total += n;
		} else if (r < 0) {
			// 552: action aborted
			answer(ctx,"552 unable to read from %s: %s\r\n",arg,strerror(errno));
			log_warn(TAG,"unable to read from %s: %s",arg,strerror(errno));
			goto cleanup;
		}
		log_dbug(TAG,"sent %d bytes",r);
	} while (r > 0);
	log_dbug(TAG,"sent %d bytes",total);
	answer(ctx,"200\r\n");
cleanup:
	close(ctx->dcon);
	ctx->dcon = -1;
	free(buf);
	close(fd);
#endif
}


static void store(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		log_warn(TAG,"store request without argument");
		answer(ctx,"501 missing argument\r\n");
		return;
	}
	if (ctx->dcon == -1) {
		answer(ctx,"501 PORT missing\r\n");
		log_warn(TAG,"store request without port");
		return;
	}
	char fn[256];
	strcpy(fn,ctx->root);
	strcat(fn,ctx->wd);
	size_t fl = strlen(fn);
	if (fn[fl-1] != '/') {
		fn[fl] = '/';
		++fl;
	}
	memcpy(fn+fl,arg,strlen(arg)+1);
#ifdef USE_FOPEN
	FILE *f = fopen(fn,"w");
	if (f == 0) {
		answer(ctx,"552 unable to create %s: %s\r\n",arg,strerror(errno));
		log_warn(TAG,"unable to open %s for storing: %s",fn,strerror(errno));
		return;
	}
	answer(ctx,"150 created %s\r\n",arg);
	char *buf = (char*)malloc(BUFSIZE);
	assert(buf);
	int n, total = 0;
	do {
		n = recv(ctx->dcon,buf,BUFSIZE,0);
		if (n > 0) {
			int w = fwrite(buf,n,1,f);
			if (-1 == w) {
				answer(ctx,"552 error writing: %s\r\n",strerror(errno));
				log_warn(TAG,"unable to write to %s for storing: %s",arg,strerror(errno));
				goto cleanup;
			}
			total += w;
		} else if (n < 0) {
			// 552: action aborted
			answer(ctx,"552 unable to write to %s: %s\r\n",arg,strerror(errno));
			log_warn(TAG,"unable to write to %s for storing: %s",arg,strerror(errno));
			goto cleanup;
		}
		log_dbug(TAG,"received and wrote %d bytes",n);
	} while (n > 0);
	log_dbug(TAG,"wrote %d bytes to %s",total,arg);
	answer(ctx,"200\r\n");
cleanup:
	close(ctx->dcon);
	ctx->dcon = -1;
	free(buf);
	fclose(f);
#else
	int fd = open(fn,O_CREAT|O_WRONLY,0666);
	if (fd == -1) {
		answer(ctx,"552 unable to create %s: %s\r\n",arg,strerror(errno));
		log_warn(TAG,"unable to open %s for storing: %s",fn,strerror(errno));
		return;
	}
	answer(ctx,"150 created %s\r\n",arg);
	char *buf = (char*)malloc(BUFSIZE);
	assert(buf);
	int n, total = 0;
	do {
		n = recv(ctx->dcon,buf,BUFSIZE,0);
		if (n > 0) {
			int w = write(fd,buf,n);
			if (-1 == w) {
				answer(ctx,"552 error writing: %s\r\n",strerror(errno));
				log_warn(TAG,"unable to write to %s for storing: %s",arg,strerror(errno));
				goto cleanup;
			}
			total += w;
		} else if (n < 0) {
			// 552: action aborted
			answer(ctx,"552 unable to write to %s: %s\r\n",arg,strerror(errno));
			log_warn(TAG,"unable to write to %s for storing: %s",arg,strerror(errno));
			goto cleanup;
		}
		log_dbug(TAG,"received and wrote %d bytes",n);
	} while (n > 0);
	log_dbug(TAG,"wrote %d bytes to %s",total,arg);
	answer(ctx,"200\r\n");
cleanup:
	close(ctx->dcon);
	ctx->dcon = -1;
	free(buf);
	close(fd);
#endif
}


static void cwd(ftpctx_t *ctx, const char *arg)
{
	log_dbug(TAG,"CWD %s",arg);
	if (arg == 0) {
		answer(ctx,"501 missing argument\r\n",4,0);
		return;
	}
	if (arg[0] == '/') {
		ctx->wd = (char*)realloc(ctx->wd,strlen(arg)+1);
		strcpy(ctx->wd,arg);
	} else {
		ctx->wd = (char*)realloc(ctx->wd,strlen(ctx->wd)+strlen(arg)+2);
		strcat(ctx->wd,arg);
		size_t al = strlen(arg);
		if (arg[al-1] != '/')
			strcat(ctx->wd,"/");
	}
	fold_path(ctx->wd);
	answer(ctx,"200 %s\r\n",ctx->wd);
	log_dbug(TAG,"cwd %s",ctx->wd);
}


static void cdup(ftpctx_t *ctx, const char *arg)
{
	log_dbug(TAG,"cdup %s",ctx->wd);
	char *sl = strrchr(ctx->wd,'/');
	if (sl == ctx->wd) {
		answer(ctx,"200 %s\r\n",ctx->wd);
		log_dbug(TAG,"cwd %s",ctx->wd);
		return;
	}
	assert(sl[1] == 0);
	sl = up_slash(ctx->wd,sl-1);
	sl[1] = 0;
	answer(ctx,"200 %s\r\n",ctx->wd);
	log_dbug(TAG,"cwd %s",ctx->wd);
}


static void dele(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		answer(ctx,"501 missing argument\r\n",4,0);
		return;
	}
	char fn[200];
	strcpy(fn,ctx->root);
	strcat(fn,ctx->wd);
	strcat(fn,arg);
#if 1 //def ESP32
	int r = unlink(fn);
	if (r == 0) {
		answer(ctx,"200 OK\r\n",4,0);
		return;
	}
	// 550: file not found or so
	log_warn(TAG,"error removing %s: %s",fn,strerror(errno));
	answer(ctx,"550 error deleting: %s\r\n",strerror(errno));
#else
	log_warn(TAG,"error removing %s: not implemented",fn);
	answer(ctx,"550 error deleting: not implemented\r\n");
#endif
}


static void list(ftpctx_t *ctx, const char *arg)
{
	log_dbug(TAG,"LIST %s",arg ? arg : ctx->wd);
	if (ctx->dcon == -1) {
		answer(ctx,"550 use port before list\r\n");
		return;
	}
	TcpTerminal term(ctx->dcon,true);
	char cmd[256];
	strcpy(cmd,"ls ");
	strcat(cmd,ctx->root);
	if (arg) {
		strcat(cmd,"/");
		strcat(cmd,arg);
	} else
		strcat(cmd,ctx->wd);
	answer(ctx,"150 ready for output\r\n");
	log_dbug(TAG,"list exe %s",cmd);
	shellexe(term,cmd);
	answer(ctx,"226 tranfer completed\r\n");
	close(ctx->dcon);
	ctx->dcon = -1;
}


static void nlst(ftpctx_t *ctx, const char *arg)
{
	log_dbug(TAG,"LIST %s",arg ? arg : ctx->wd);
	if (ctx->dcon == -1) {
		answer(ctx,"550 use port before list\r\n");
		return;
	}
	TcpTerminal term(ctx->dcon,true);
	char cmd[256];
	strcpy(cmd,"ls -1 ");
	strcat(cmd,ctx->root);
	if (arg) {
		strcat(cmd,"/");
		strcat(cmd,arg);
	} else
		strcat(cmd,ctx->wd);
	answer(ctx,"150 ready for output\r\n");
	log_dbug(TAG,"nlst exe %s",cmd);
	shellexe(term,cmd);
	answer(ctx,"226 tranfer completed\r\n");
	close(ctx->dcon);
	ctx->dcon = -1;
}


static void port(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		answer(ctx,"501 missing argumetn\r\n");
		return;
	}
	unsigned a0,a1,a2,a3,p0,p1;
	if (6 != sscanf(arg,"%u,%u,%u,%u,%u,%u",&a0,&a1,&a2,&a3,&p0,&p1)) {
		log_error(TAG,"PORT expected 6 arguments, got '%s'",arg);
		// 501: syntax/argument error
		answer(ctx,"501 invalid argument %s\r\n",arg);
		return;
	}
	log_dbug(TAG,"port %u.%u.%u.%u:%u",a0,a1,a2,a3,p0<<8|p1);
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		// 425: cannot open connection
		answer(ctx,"425\r\n");
		log_error(TAG,"error creating ftp PORT: %s",strneterr(sock));
		return;
	}
	uint32_t a = a0 << 24 | a1 << 16 | a2 << 8 | a3;
	uint16_t p = p0 << 8 | p1;
	struct sockaddr_in port_addr;
	port_addr.sin_family = AF_INET;
	port_addr.sin_port = htons(p);
	port_addr.sin_addr.s_addr = htonl(a);
	if (connect(sock, (struct sockaddr *)&port_addr, sizeof(port_addr)) < 0) {
		answer(ctx,"425 error bind port: %s\r\n",strneterr(sock));
		log_error(TAG,"error binding ftp PORT: %s",strneterr(sock));
		return;
	}
	if (ctx->dcon != -1)
		close(ctx->dcon);
	ctx->dcon = sock;
	log_dbug(TAG,"PORT created socket %d",sock);
	answer(ctx,"200 PORT OK\r\n");
}


static void size(ftpctx_t *ctx, const char *arg)
{
	if (arg == 0) {
		answer(ctx,"501 missing argumetn\r\n");
		return;
	}
	char *fn = arg2fn(ctx,arg);
#ifdef USE_FOPEN
	FILE *f = fopen(fn,"r");
	if (f == 0) {
		answer(ctx,"550 error accessing %s: %s\r\n",fn,strerror(errno));
		free(fn);
		return;
	}
	if (-1 == fseek(f,0,SEEK_END)) {
		answer(ctx,"550 error seeking%s: %s\r\n",fn,strerror(errno));
		free(fn);
		return;
	}
	free(fn);
	answer(ctx,"213 %u\r\n",ftell(f));
	fclose(f);
#else
	struct stat st;
	int r = stat(fn,&st);
	free(fn);
	if (r == 0)
		answer(ctx,"213 %u\r\n",st.st_size);
	else
		answer(ctx,"550 error stating: %s\r\n",strerror(errno));
#endif
}


static void passive(ftpctx_t *ctx, const char *arg)
{
	// TODO
	// 202: not implemented
	answer(ctx,"202 not implemented\r\n");
}


static void user(ftpctx_t *ctx, const char *arg)
{
	ctx->login = 0;
	if (0 == strcmp(arg,"ftp")) {
		if (Config.has_pass_hash() && !verifyPassword("")) {
			answer(ctx,"530 user ftp not allowed\r\n");
		} else {
			answer(ctx,"230 login ok\r\n");
			ctx->login = 3;
		}
	} else if (0 == strcmp(arg,"root")){
		answer(ctx,"331 password required\r\n");
		ctx->login = 4;
	}
}


static void pass(ftpctx_t *ctx, const char *arg)
{
	if ((ctx->login == 4) && verifyPassword(arg)) {
		ctx->login = 5;
		answer(ctx,"230 pass ok\r\n");
	} else {
		answer(ctx,"530 invalid password\r\n");
	}
}


static void syst(ftpctx_t *ctx, const char *arg)
{
	answer(ctx,"200 Type: esp32\r\n");
}


static void quit(ftpctx_t *ctx, const char *arg)
{
	answer(ctx,"221\r\n");
	close(ctx->con);
	ctx->con = -1;
	free(ctx->wd);
	vTaskDelete(0);
}


static const ftpcom_t Commands[] = {
	{ "RETR", retrive },
	{ "STOR", store },
	{ "LIST", list },
	{ "NLST", nlst },
	{ "PORT", port },
	{ "PASV", passive },
	{ "QUIT", quit },
	{ "DELE", dele },
	{ "PWD", pwd },
	{ "MKD", mkd },
	{ "RMD", rmd },
	{ "CWD", cwd },
	{ "CDUP", cdup },
	{ "SYST", syst },
	{ "USER", user },
	{ "PASS", pass },
	{ "RNFR", rnfr },
	{ "RNTO", rnto },
	{ "NOOP", noop },
	{ "SIZE", size },
};


static int execute_buffer(ftpctx_t *ctx, char *buf, int fill)
{
	log_dbug(TAG,"ftpd exe '%s'",buf);
	char *eol = streol(buf,fill);
	while (eol) {
		*eol = 0;
		++eol;
		if ((*eol == '\r') || (*eol == '\n'))
			++eol;
		log_dbug(TAG,"ftpd('%s')",buf);
		char *sp = strchr(buf,' ');
		if (sp) 
			*sp = 0;
		bool found = false;
		if ((ctx->login&1) == 0) {
			if (0 == strcmp(buf,"USER")) 
				user(ctx,sp ? sp+1 : 0);
			else if (0 == strcmp(buf,"PASS"))
				pass(ctx,sp ? sp+1 : 0);
			else
				answer(ctx,"530 user not logged in\r\n");
			found = true;
		} else for (int i = 0; i < sizeof(Commands)/sizeof(Commands[0]); ++i) {
			if (0 == strcmp(Commands[i].cmd,buf)) {
				Commands[i].fun(ctx,sp ? sp+1 : 0);
				found = true;
				break;
			}
		}
		if (!found) {
			log_warn(TAG,"unsupported command %s",buf);
			answer(ctx,"502 unknown command %s\r\n",buf);
		}
		fill -= eol-buf;
		memmove(buf,eol,fill);
		eol = streol(buf,fill);
	}
	return fill;
}


void ftpd_session(void *arg)
{
	int con = (int)arg;
	ftpctx_t ctxt;
	ctxt.con = con;
	ctxt.dcon = -1;
	ctxt.wd = strdup("/");
	ctxt.root = Config.ftpd().root().c_str();
	ctxt.rnfr = 0;
	char buf[256];
	size_t fill = 0;
	log_dbug(TAG,"starting session");
	answer(&ctxt,"220 Connection established.\r\n");
	for (;;) {
		int n = recv(con,buf+fill,sizeof(buf)-fill-1,0);
		if (n < 0) {
			log_dbug(TAG,"error on recv: %s",strneterr(con));
			close(con);
			vTaskDelete(0);
			return;
		}
		if (n == 0)
			continue;
		fill += n;
		buf[fill] = 0;
		fill = execute_buffer(&ctxt,buf,fill);
		if (fill == -1) {
			log_dbug(TAG,"error executing");
			close(con);
			vTaskDelete(0);
			return;
		}
	}
}


int ftpd_setup()
{
	if (!Config.has_ftpd())
		return 0;
	const FtpHttpConfig &c = Config.ftpd();
	uint16_t p = c.port();
	if (!c.has_root() || (0 == p))
		return 0;
	const char *r = c.root().c_str();
	struct stat st;
	if (-1 == stat(r,&st)) {
		log_warn(TAG,"error accessing root '%s': %s",r,strerror(errno));
		return 1;
	}
	log_info(TAG,"port %hu, root %s",p,r);
	return listen_port(p,m_tcp,ftpd_session,"ftpd","_ftp",4,4096);
}

#endif
