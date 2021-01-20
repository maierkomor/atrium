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

#include "HttpResp.h"

#include "log.h"
#include "netsvc.h"
#include "profiling.h"
#include "support.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFSIZE 1024

using namespace std;

static char TAG[] = "httpa";
static char CRNL[] = "\r\n";

const char HTTP_OK[] =           "200 OK";
const char HTTP_CREATED[] =      "201 Created";
const char HTTP_ACCEPT[] =       "202 Accepted";
const char HTTP_NO_CONT[] =      "204 No Content";
const char HTTP_RST_CONT[] =     "205 Reset Content";
const char HTTP_BAD_REQ[] =      "400 Bad Request";
const char HTTP_UNAUTH[] =       "401 Unauthorized";
const char HTTP_FORBIDDEN[] =    "403 Forbidden";
const char HTTP_NOT_FOUND[] =    "404 Not Found";
const char HTTP_TOO_LONG[] =     "414 URI Too Long";
const char HTTP_2MANY_REQ[] =    "429 Too Many Requests";
const char HTTP_LARGE_HDR[] =    "431 Request Header Fields Too Large";
const char HTTP_INTERNAL_ERR[] = "500 Internal Server Error";
const char HTTP_NOT_IMPL[] =     "501 Not Implemented";
const char HTTP_SVC_UNAVAIL[] =  "503 Service Unavailable";
const char HTTP_INV_VERSION[] =  "505 HTTP Version Not Supported";
const char HTTP_INSUF_SPACE[] =  "507 Insufficient Storage";
const char HTTP_NETAUTH_REQ[] =  "511 Network Authentication Required";

const char CT_APP_JSON[]		= "application/json";
const char CT_APP_OCTET_STREAM[]	= "application/octet-stream";
const char CT_TEXT_HTML[]		= "text/html";
const char CT_TEXT_PLAIN[]		= "text/plain";
const char CT_TEXT_CSS[]		= "text/css";
const char CT_TEXT_CSV[]		= "text/csv";
const char CT_IMAGE_JPEG[]		= "image/jpeg";


HttpResponse::HttpResponse()
: m_result(0)
, m_header()
, m_content()
, m_fd(-1)
, m_con(-1)
, m_reslen(0)
{

}


HttpResponse::~HttpResponse()
{
	if (m_reslen)
		free(m_result);
}


void HttpResponse::addHeader(const char *h)
{
	m_header += h;
	m_header += CRNL;
}


void HttpResponse::setContentType(const char *t)
{
	m_header += "Content-Type: ";
	m_header += t;
	m_header += CRNL;
}


void HttpResponse::setContentLength(unsigned s)
{
	m_header += "Content-Length: ";
	char buf[16];
	sprintf(buf,"%u",s);
	m_header += buf;
	m_header += CRNL;
}


void HttpResponse::setResult(const char *fmt, ...)
{
	if (m_reslen)
		free(m_result);
	if (0 == strchr(fmt,'%')) {
		m_result = (char*)fmt;
		return;
	}
	va_list val;
	va_start(val,fmt);
	m_reslen = vasprintf(&m_result,fmt,val);
	va_end(val);
}


/*
void HttpResponse::writeHeader(const char *fmt, ...)
{
	char buf[128];
	va_list val;
	va_start(val,fmt);
	int n = vsnprintf(buf,sizeof(buf),fmt,val);
	if (n < sizeof(buf)) {
		m_header += buf;
	} else {
		size_t s = m_header.size();
		m_header.resize(s + n, ' ');
		vsprintf((char*)m_header.data()+s,fmt,val);
	}
	va_end(val);
	m_header += CRNL;
}
*/


void HttpResponse::addContent(const char *h, size_t l)
{
	log_dbug(TAG,"addContent: %u bytes",l);
	if (l == 0)
		l = strlen(h);
	if (m_con != -1) {
		if (-1 == send(m_con,h,l,0)) {
			log_warn(TAG,"error sending: %s",strneterr(m_con));
			close(m_con);
			m_con = -1;
		}
	} else {
		m_content.append(h,l);
	}
}


void HttpResponse::writeContent(const char *fmt, ...)
{
	char buf[128];
	va_list val;
	va_start(val,fmt);
	int n = vsnprintf(buf,sizeof(buf)-1,fmt,val);
	if (n < sizeof(buf)) {
		if (m_con == -1) {
			m_content.append(buf,n);
		} else if (-1 == send(m_con,buf,n,0)) {
			log_error(TAG,"error sending content: %s",strneterr(errno));
		}
	} else {
		if (m_con != -1)
			m_content.clear();
		size_t s = m_content.size();
		m_content.resize(s + n, 0);
		vsprintf((char*)m_content.data()+s,fmt,val);
		if ((m_con != -1) && (-1 == send(m_con,m_content.data(),s+n,0)))
			log_warn(TAG,"error sending content: %s",strneterr(errno));
	}
	va_end(val);
}


bool HttpResponse::senddata(int con, int fd)
{
	//TimeDelta td(__FUNCTION__);
	m_con = con;
	size_t cs = m_content.size();
	assert((fd == -1) || (cs == 0));
	if (fd != -1) {
		struct stat st;
		if (-1 == fstat(fd,&st)) {
			log_error(TAG,"unable to stat file: %s",strerror(errno));
			setResult("500 unable to access file: %s",strerror(errno));
		} else {
			cs = st.st_size;
		}
	}
	assert(m_result != 0);
	char msg[128];
	int n;
	if (cs > 0) {
		n = snprintf(msg,sizeof(msg),"HTTP/1.1 %s\r\nContent-Length: %u\r\n%s",m_result,cs,m_header.empty() ? CRNL : "");
	} else { 
		n = snprintf(msg,sizeof(msg),"HTTP/1.1 %s\r\n%s",m_result,m_header.empty() ? CRNL : "");
	}
	if (n > sizeof(msg)) {
		log_error(TAG,"temporary buffer too small");
		return false;
	}
	log_dbug(TAG,"sending:>>\n%s\n<<",msg);
	int r = send(con,msg,n,0);
	if (-1 == r) {
//		log_error(TAG,"error sending: %s",strerror(errno));
		return false;
	}
	if (!m_header.empty()) {
		m_header += CRNL;
		if (-1 == send(con,m_header.data(),m_header.size(),0)) {
			log_error(TAG,"error sending: %s",strerror(errno));
			return false;
		}
	}
	if (fd != -1) {
		char *buf = (char*)malloc(BUFSIZE);
		while (cs > 0) {
			size_t bs = BUFSIZE < cs ? BUFSIZE : cs;
			int n = read(fd,buf,bs);
			cs -= bs;
			if (n == -1) 
				memset(buf,0,bs);
			if (-1 == send(con,buf,bs,0)) {
				free(buf);
				log_error(TAG,"error sending content: %s",strneterr(con));
				return false;
			}
		}
		free(buf);
	} else if (!m_content.empty()) {
		if (-1 == send(con,m_content.data(),m_content.size(),0)) {
			log_error(TAG,"error sending: %s",strerror(errno));
			return false;
		}
	}
	log_dbug(TAG,"sent answer");
	return true;
}
