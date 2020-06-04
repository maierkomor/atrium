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

#include "HttpServer.h"
#include "HttpReq.h"
#include "HttpResp.h"
#include "romfs.h"

#include "log.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>

#if defined CONFIG_FATFS || defined CONFIG_SPIFFS
#define HAVE_FS
#endif


using namespace std;


static char TAG[] = "www";



HttpServer::HttpServer(const char *wwwroot, const char *rootmap)
: m_wwwroot(wwwroot)
, m_rootmap(rootmap)
, m_upload(0)
, m_files()
, m_dirs()
{
}


void HttpServer::addDirectory(const char *d)
{
	m_dirs.insert(d);
}


void HttpServer::addFile(const char *f)
{
	m_files.insert(strdup(f));
}


void HttpServer::addMemory(const char *name, const char *contents)
{
	m_memfiles.insert(make_pair(name,contents));
}


void HttpServer::addFunction(const char *name, www_fun_t f)
{
	m_functions.insert(make_pair(name,f));
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
	int fd = open(fn,O_RDONLY);
	if (fd == -1) {
		log_info(TAG,"open of %s failed",fn);
		ans.setResult(HTTP_NOT_FOUND);
		ans.senddata(req->getConnection());
	} else {
		log_info(TAG,"sending file");
		ans.setResult(HTTP_OK);
		ans.senddata(req->getConnection(),fd);
	}
	return true;
#else
	return false;
#endif
}


bool HttpServer::runFile(HttpRequest *req)
{
#ifdef HAVE_FS
	const char *uri = req->getURI();
	log_info(TAG,"looking for file %s",uri);
	auto i = m_files.find(uri);
	if (i == m_files.end()) {
		for (auto x = m_files.begin(), y = m_files.end(); x != y; ++x) {
			log_info(TAG,"file %s",*x);
		}
		log_info(TAG,"file not found");
		return false;
	}
	if (m_wwwroot == 0) {
		log_error(TAG,"looking for file but no wwwroot given");
		return false;
	}
	size_t rl = strlen(m_wwwroot);
	if (m_wwwroot[rl-1] == '/')
		--rl;
	size_t ul = strlen(uri);
	char fn[ul+rl+1];
	memcpy(fn,m_wwwroot,rl);
	memcpy(fn+rl,uri,ul+1);
	log_info(TAG,"open(%s)",fn);
	HttpResponse ans;
	int fd = open(fn,O_RDONLY);
	if (fd == -1) {
		log_info(TAG,"open of %s failed",fn);
		ans.setResult(HTTP_NOT_FOUND);
		ans.senddata(req->getConnection());
	} else {
		log_info(TAG,"sending file");
		ans.setResult(HTTP_OK);
		ans.senddata(req->getConnection(),fd);
		close(fd);
	}
	return true;
#else
	const char *uri = req->getURI();
	if (*uri == '/')
		++uri;
	int r = romfs_open(uri);
	if (r < 0) {
		log_info(TAG,"%s is not in romfs",uri);
		return false;
	}
	size_t s = romfs_size(r);
	log_info(TAG,"found %s in romfs, size %u",uri,s);
	HttpResponse ans;
	int con = req->getConnection();
	if (s <= 512) {
		ans.setResult(HTTP_OK);
		string &content = ans.contentString();
		content.resize(s);
		romfs_read_at(r,(char*)content.data(),s,0);
		ans.senddata(con);
	} else {
		const unsigned bs = 512;
		char tmp[bs];
		ans.setResult(HTTP_OK);
		ans.setContentLength(s);
		ans.senddata(con);
		int off = 0;
		do {
			unsigned n = s > sizeof(tmp) ? sizeof(tmp) : s;
			romfs_read_at(r,tmp,n,off);
			off += n;
			if (-1 == send(con,tmp,n,0)) {
				log_error(TAG,"error sending: %s",strerror(errno));
				return false;
			}
			//ans.addContent(tmp,n);
			s -= n;
		} while (s > 0);
	}
	return true;
#endif
}


bool HttpServer::runFunction(HttpRequest *req)
{
	auto i = m_functions.find(req->getURI());
	if (i == m_functions.end()) {
		//log_info(TAG,"function not found");
		return false;
	}
	www_fun_t f = i->second;
	f(req);
	return true;
}


bool HttpServer::runMemory(HttpRequest *req)
{
	auto m = m_memfiles.find(req->getURI());
	if (m == m_memfiles.end()) {
		//log_info(TAG,"memfile not found");
		return false;
	}
	HttpResponse ans;
	ans.setResult(HTTP_OK);
	ans.addContent(m->second);
	ans.senddata(req->getConnection());
	return true;
}


void HttpServer::performGET(HttpRequest *req)
{
	const char *uri = req->getURI();
	//log_info(TAG,"get %s",uri);
	if ((uri[0] == '/') && (uri[1] == 0)) {
		log_info(TAG,"root query mapped to %s",m_rootmap);
		req->setURI(m_rootmap);
	}
	if (runMemory(req)) {
	} else if (runFunction(req)) {
	} else if (runFile(req)) {
	} else if (runDirectory(req)) {
	} else {
		log_info(TAG,"not found");
		HttpResponse ans;
		ans.setResult(HTTP_NOT_FOUND);
		ans.senddata(req->getConnection());
	}
}


void HttpServer::performPOST(HttpRequest *req)
{
	log_info(TAG,"post request %s",req->getURI());
	if (!runFunction(req)) {
		HttpResponse ans;
		ans.setResult(HTTP_NOT_FOUND);
		ans.senddata(req->getConnection());
	}
}


void HttpServer::performPUT(HttpRequest *req)
{
	const char *uri = req->getURI();
	log_info(TAG,"put request %s",uri);
	const char *sl = strrchr(uri,'/');
	char fn[strlen(m_wwwroot)+strlen(sl)];
	int fd = open(fn,O_CREAT|O_WRONLY,0666);
	int con = req->getConnection();
	HttpResponse ans;
	if (fd == -1) {
		log_warn(TAG,"unable to create upload file %s: %s",fn,strerror(errno));
		ans.setResult(HTTP_NOT_FOUND);
		ans.senddata(con);
		return;
	}
	ans.setResult(HTTP_CREATED);
	ans.senddata(con);
	if (-1 == write(fd,req->getContent(),req->getAvailableLength())) {
		close(fd);
		log_warn(TAG,"unable to write to upload file %s: %s",fn,strerror(errno));
		ans.setResult(errno == ENOSPC ? HTTP_INSUF_SPACE : HTTP_INTERNAL_ERR);
		ans.senddata(con);
		return;
	}
	int n;
	do {
		char buf[1024];
		n = recv(con,buf,sizeof(buf),0);
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
	ans.senddata(con);

}


void HttpServer::handleConnection(int con)
{
	unsigned count = 0;
	log_info(TAG,"new incoming connection");
	HttpRequest *req = HttpRequest::parseRequest(con);
	while (req && (req->getError() == 0)) {
		httpreq_t t = req->getType();
		if (t == hq_put) {
			performPUT(req);
		} else if (t == hq_post) {
			performPOST(req);
		} else if (t == hq_get) {
			performGET(req);
		} else if (t == hq_delete) {
			string fn = m_wwwroot;
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
		if (++count > 15)	// limit connection to 8 requests
			break;
		req = HttpRequest::parseRequest(con);
	}
	log_info(TAG,"closing connection");
	close(con);
}


