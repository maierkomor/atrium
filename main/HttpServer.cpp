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

#include "HttpServer.h"
#include "HttpReq.h"
#include "HttpResp.h"
#include "lwtcp.h"
#include "romfs.h"

#include "log.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>

#define HTTP_REQ_SIZE 2048

#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
#define HAVE_FS
#endif

#ifdef read
#undef read
#endif

#ifdef write
#undef write
#endif

#if 0
#define log_devel log_dbug
#else
#define log_devel(...)
#endif

using namespace std;


#define TAG MODULE_WWW


HttpServer::HttpServer(const char *wwwroot, const char *rootmap)
: m_wwwroot(wwwroot)
, m_rootmap(rootmap)
, m_upload(0)
, m_dirs()
, m_rootlen(wwwroot ? strlen(wwwroot) : 0)
{
	if (rootmap == 0)
		rootmap = "/index.html";
	assert(rootmap[0] == '/');
}


void HttpServer::addDirectory(const char *d)
{
	m_dirs.insert(d);
}


void HttpServer::addFunction(const char *name, www_fun_t f)
{
#if IDF_VERSION >= 50
	m_functions[name] = f;
#else
	m_functions.insert(pair<const char *,www_fun_t>(name,f));
#endif
}


void HttpServer::setUploadDir(const char *ud)
{
	m_upload = ud;
}


bool HttpServer::runDirectory(HttpRequest *req)
{
#ifdef HAVE_FS
	const char *uri = req->getURI();
	set<const char *,SubstrLess>::iterator i = m_dirs.find(uri);
	if (i == m_dirs.end()) {
		log_info(TAG,"dir not found");
		return false;
	}
	size_t rl = strlen(m_wwwroot);
	if (m_wwwroot[rl-1] == '/')
		--rl;
	size_t ul = strlen(uri);
	char fn[ul+rl+1];
	memcpy(fn,m_wwwroot,rl);
	memcpy(fn+rl,uri,ul+1);
	HttpResponse ans;
	const char *res;
	int fd = open(fn,O_RDONLY);
	if (fd == -1) {
		log_warn(TAG,"open of %s failed",fn);
		res = HTTP_NOT_FOUND;
	} else {
		log_info(TAG,"sending file %s",fn);
		res = HTTP_OK;
	}
	ans.setResult(res);
	ans.senddata(req->getConnection(),fd);
	return true;
#else
	return false;
#endif
}


#ifdef CONFIG_ROMFS
static void send_romfs(int r, HttpRequest *req)
{
	HttpResponse ans;
	LwTcp *con = req->getConnection();
	const char *uri = req->getURI() + 1;
	if (strstr(uri,".gz"))
		ans.addHeader("Content-Encoding:gzip");
	ans.setResult(HTTP_OK);
	if (0 == strstr(uri,".html"))
		ans.setContentType(CT_TEXT_PLAIN);
	ssize_t s = romfs_size_fd(r);
	ans.setContentLength(s);
	ans.senddata(con);
#ifdef CONFIG_ENABLE_FLASH_MMAP
	char *addr = (char *) romfs_mmap(r);
	log_dbug(TAG,"mmaped romfs file to %p",addr);
	if (-1 == con->write(addr,s,true)) {
		log_warn(TAG,"error sending: %s",strerror(errno));
		return;
	}
#else
	int off = 0;
	char tmp[512];
	do {
		unsigned n = s > sizeof(tmp) ? sizeof(tmp) : s;
		romfs_read_at(r,tmp,n,off);
		off += n;
		// no copy, force sync
		if (-1 == con->write(tmp,n,false)) {
			log_warn(TAG,"error sending: %s",con->error());
			return;
		}
		con->sync(true);
		s -= n;
	} while (s > 0);
#endif // CONFIG_ENABLE_FLASH_MMAP
}
#endif // CONFIG_ROMFS


bool HttpServer::runFile(HttpRequest *req)
{
#if defined CONFIG_ROMFS || defined HAVE_FS
	const char *uri = req->getURI();
	assert(*uri == '/');
#endif
#ifdef CONFIG_ROMFS
	int r = romfs_open(uri+1);
	if (r >= 0) {
		log_dbug(TAG,"found %s in romfs",uri);
		send_romfs(r,req);
		return true;
	}
#endif // CONFIG_ROMFS
#ifdef HAVE_FS
	if (m_wwwroot == 0) {
		log_warn(TAG,"wwwroot not set");
		return false;
	}
	size_t ul = strlen(uri);
	char path[m_rootlen + ul + 1];
	memcpy(path,m_wwwroot,m_rootlen);
	memcpy(path+m_rootlen,uri,ul+1);
	log_dbug(TAG,"open(%s)",path);
	int fd = open(path,O_RDONLY);
	if (fd != -1) {
		log_dbug(TAG,"sending file %s",path);
		HttpResponse ans;
		ans.setResult(HTTP_OK);
		ans.senddata(req->getConnection(),fd);
		close(fd);
		return true;
	}
#endif // HAVE_FS
	return false;
}


bool HttpServer::runFunction(HttpRequest *req)
{
	auto i = m_functions.find(req->getURI());
	if (i == m_functions.end()) {
		return false;
	}
	log_dbug(TAG,"function %s",req->getURI());
	www_fun_t f = i->second;
	f(req);
	return true;
}


#ifdef WITH_MEMFILES
void HttpServer::addMemory(const char *name, const char *contents)
{
	m_memfiles.insert(make_pair(name,contents));
}


bool HttpServer::runMemory(HttpRequest *req)
{
	auto m = m_memfiles.find(req->getURI());
	bool r = false;
	if (m != m_memfiles.end()) {
		log_dbug(TAG,"memfile %s",req->getURI());
		HttpResponse ans;
		ans.setResult(HTTP_OK);
		ans.addContent(m->second);
		ans.senddata(req->getConnection());
		r = true;
	}
	return r;
}
#endif


void HttpServer::performGET(HttpRequest *req)
{
	const char *uri = req->getURI();
	//log_info(TAG,"get %s",uri);
	if ((uri[0] == '/') && (uri[1] == 0)) {
		log_dbug(TAG,"root query mapped to %s",m_rootmap);
		req->setURI(m_rootmap);
	}
	if (runFunction(req)) {
	} else if (runFile(req)) {
	} else if (runDirectory(req)) {
#ifdef WITH_MEMFILES
	} else if (runMemory(req)) {
#endif
	} else {
		log_dbug(TAG,"%s: not found",uri);
		HttpResponse ans;
		ans.setResult(HTTP_NOT_FOUND);
		ans.senddata(req->getConnection());
	}
}


void HttpServer::performPOST(HttpRequest *req)
{
	log_devel(TAG,"POST %s",req->getURI());
	if (!runFunction(req)) {
		HttpResponse ans;
		ans.setResult(HTTP_NOT_FOUND);
		ans.senddata(req->getConnection());
	}
}


void HttpServer::performPUT(HttpRequest *req)
{
#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
	const char *uri = req->getURI();
	log_devel(TAG,"put request %s",uri);
	const char *sl = strrchr(uri,'/');
	char fn[strlen(m_wwwroot)+strlen(sl)];
	strcpy(fn,m_wwwroot);
	strcat(fn,sl);
	int fd = open(fn,O_CREAT|O_WRONLY,0666);
	LwTcp *con = req->getConnection();
	HttpResponse ans;
	if (fd == -1) {
		log_warn(TAG,"create upload file %s: %s",fn,strerror(errno));
		ans.setResult(HTTP_NOT_FOUND);
		ans.senddata(con);
		return;
	}
	ans.setResult(HTTP_CREATED);
	ans.senddata(con);
	if (-1 == write(fd,req->getContent(),req->getAvailableLength())) {
		close(fd);
		log_warn(TAG,"write upload file %s: %s",fn,strerror(errno));
		ans.setResult(errno == ENOSPC ? HTTP_INSUF_SPACE : HTTP_INTERNAL_ERR);
	} else {
		int n;
		do {
			char buf[1024];
			n = con->read(buf,sizeof(buf));
			if (n > 0)
				n = write(fd,buf,n);
		} while (n > 0);
		close(fd);
		if (n == 0)
			ans.setResult(HTTP_OK);
		else if (errno == ENOSPC)
			ans.setResult(HTTP_INSUF_SPACE);
		else
			ans.setResult(HTTP_INTERNAL_ERR);
	}
	ans.senddata(con);
#else
	HttpResponse ans;
	log_warn(TAG,"no SPIFFS - cannot upload");
	ans.setResult(HTTP_NOT_IMPL);
	LwTcp *con = req->getConnection();
	ans.senddata(con);
#endif
}


void HttpServer::handleConnection(LwTcp *con)
{
	char *buf = (char *) malloc(HTTP_REQ_SIZE);
	if (buf == 0) {
		log_warn(TAG,"Out of memory.");
		return;
	}
	unsigned count = 0;
	log_dbug(TAG,"new incoming connection");
	HttpRequest *req = HttpRequest::parseRequest(con,buf,HTTP_REQ_SIZE);
	while (req && (req->getError() == 0)) {
		httpreq_t t = req->getType();
		if (t == hq_put) {
			performPUT(req);
		} else if (t == hq_post) {
			performPOST(req);
		} else if (t == hq_get) {
			performGET(req);
		} else if (t == hq_delete) {
			estring fn = m_wwwroot;
			fn += req->getURI();
			HttpResponse ans;
			if (unlink(fn.c_str()) == 0)
				ans.setResult(HTTP_OK);
			else if (errno == ENOENT)
				ans.setResult(HTTP_NOT_FOUND);
			else
				ans.setResult(HTTP_INTERNAL_ERR);
		} else {
			HttpResponse ans;
			ans.setResult(HTTP_BAD_REQ);
			if (!ans.senddata(con))
				break;
		}
		bool a = req->keepAlive();
		bool e = req->getError() != 0;
		delete req;
		if (!a || e)
			break;
		if (++count > 20)	// limit connection to 10 requests
			break;
		req = HttpRequest::parseRequest(con,buf,HTTP_REQ_SIZE);
	}
	free(buf);
}


