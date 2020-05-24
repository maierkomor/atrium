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

#include "HttpReq.h"

#include "log.h"
#include "support.h"
#include "profiling.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

static char TAG[] = "httpq";
static const string Empty = "";


HttpRequest::HttpRequest(int con)
: m_con(con)
, m_httpreq(hq_none)
, m_httpver(hv_none)
, m_URI(0)
, m_contlen(-1)
, m_clen0(0)
, m_content(0)
, m_error(0)
, m_keepalive(true)
{

}


HttpRequest::~HttpRequest()
{
	if (m_URI)
		free(m_URI);
	if (m_content)
		free(m_content);
}


static bool isTCHAR(char c)
{
	if (isalnum(c) || (c == '!') || (c == '#') || (c == '$') 
		|| (c == '%') || (c == '&') || (c == '\'') 
		|| (c == '*') || (c == '+') || (c == '-')
		|| (c == '.') || (c == '^') || (c == '_')
		|| (c == '`') || (c == '|') || (c == '~'))
		return true;
	return false;
}


static char *parsePercent(char *at, string &value)
{
	assert(at[0] == '%');
	if (at[1] != 'x')
		return 0;
	uint8_t x;
	if ((at[2] >= '0') && (at[2] <= '9'))
		x = (at[2]-'0') << 4;
	else if ((at[2] >= 'A') && (at[2] <= 'F'))
		x = (at[2]-'A') << 4;
	else
		return 0;
	if ((at[3] >= '0') && (at[3] <= '9'))
		x |= (at[3]-'0');
	else if ((at[3] >= 'A') && (at[3] <= 'F'))
		x |= (at[3]-'A');
	else
		return 0;
	value += x;
	return at + 4;
}


void HttpRequest::parseField(char *at, char *end)
{
	char fn[128], *f = fn;
	char c = *at;
	// parse field name
	while (c != ':') {
		if ((f-fn) == sizeof(fn)) {
			m_error = "field name too long";
			return;
			}
		if (isTCHAR(c)) {
			*f++ = c;
		} else {
			m_error = "invalid field name character";
			return;
		}
		if (++at == end) {
			m_error = "missing field value";
			return;
		}
		c = *at;
	}
	*f++ = 0;
	if (f-fn == 1) {
		m_error = "missing field name";
		return;
	}
	++at;	// skip ':'
	// remove OWS
	while ((*at == ' ') || (*at == '\t'))
		++at;
	// remove TWS
	while ((end[-1] == ' ') | (end[-1] == '\t'))
		--end;
	if ((0 == strcmp(fn,"Accept-Encoding"))
		|| (0 == strcmp(fn,"Accept-Language"))
		|| (0 == strcmp(fn,"Accept"))
		|| (0 == strcmp(fn,"Host"))
		|| (0 == strcmp(fn,"Referer"))
		|| (0 == strcmp(fn,"Update-Insecure-Requests"))
	    )
		return;
	// parse field value
	string value;
	if (*at == '"') {
		if (end[-1] != '"') {
			m_error = "missing terminating quote in header value";
			return;
		}
		--end;
		*end = 0;
		++at;
	}
	if (end[-1] == '\\') {
		m_error = "trailing backslash in header value";
		return;
	}
	//log_info(TAG,"at = %s",at);
	c = *at;
	while (at && (c != '"') && (c != 0)) {
		if (c == '\\') {
			++at;
			value += *at;
			++at;
		} else if (c == '%') {
			at = parsePercent(at,value);
		} else {
			value += c;
			++at;
		}
		if (at)
			c = *at;
		else
			m_error = "header value parser error";
	}
	//log_info(TAG,"adding pair '%s' -> '%s'",fn,value.c_str());
	m_headers.insert(make_pair(fn,value));
}


char *HttpRequest::parseHeader(char *at, size_t s)
{
	char *cr = (char*)memchr(at,'\r',s);
	if (cr[1] != '\n') {
		m_error = "stray carriage-return";
		return 0;
	}
	if (0 == strncasecmp(at,"content-length:",15)) {
		errno = 0;
		long l = strtol(at+15,0,0);
		if (errno != 0) {
			m_error = "invalid value for content-length";
			return 0;
		}
		m_contlen = l;
	} else if (0 == strncasecmp(at,"connection:",11)) {
		char *x = at + 12;
		while ((*x == ' ') || (*x == '\t'))
			++x;
		if (!strncasecmp("keep-alive\r\n",x,12))
			m_keepalive = true;
		else if (!strncasecmp("close\r\n",x,7))
			m_keepalive = false;
		else {
			log_warn(TAG,"invalid argument for header field connection",x);
		}
	} else if (0 == strncasecmp(at,"Cache-Control:",14)) {
		// ignored completely for now
	} else if (0 == strncasecmp(at,"User-Agent:",11)) {
		// ignored completely for now
	} else {
		*cr = 0;
		parseField(at,cr);
	}
	return cr ? cr + 2 : 0;
}


const string &HttpRequest::getHeader(const char *n) const
{
	auto i = m_headers.find(n);
	if (i == m_headers.end())
		return Empty;
	return i->second;
}


size_t HttpRequest::numArgs()
{
	if ((m_httpreq == hq_post) && m_args.empty())
		parseArgs((char*)m_content);
	return m_args.size();
}


const string &HttpRequest::arg(size_t i) const
{
	if (i >= m_args.size())
		return Empty;
	return m_args[i].second;
}


const string &HttpRequest::arg(const char *a) const
{
	for (size_t i = 0, e = m_args.size(); i < e; ++i) {
		if (0 == strcmp(a,m_args[i].first.c_str()))
			return m_args[i].second;
	}
	return Empty;
}


const string &HttpRequest::argName(size_t i) const
{
	if (i >= m_args.size())
		return Empty;
	return m_args[i].first;
}


void HttpRequest::parseArgs(const char *str)
{
	//TimeDelta td(__FUNCTION__);
	while (str) {
		const char *e = strchr(str,'=');
		const char *a = e ? strchr(e+1,'&') : 0;
		if (e && a) {
			m_args.push_back(make_pair(string(str,e),string(e+1,a)));
			log_dbug(TAG,"add arg %s",m_args.back().second.c_str());
			str = a + 1;
		} else if (e) {
			m_args.push_back(make_pair(string(str,e),string(e+1)));
			log_dbug(TAG,"add larg %s",m_args.back().second.c_str());
			str = 0;
		} else
			str = 0;
	}
}


char *HttpRequest::getContent()
{
	if (m_content == 0) {
		log_error(TAG,"request to query content after content was discarded");
		assert(0);
	}
	m_content[m_contlen] = 0;
	return m_content;
}


void HttpRequest::fillContent()
{
//	TimeDelta td(__FUNCTION__);
	m_content = (char *)realloc(m_content,m_contlen+1);
	m_content[m_contlen] = 0;
	if (m_content == 0) {
		log_error(TAG,"out of memory while download content");
		m_clen0 = 0;
		return;
	}
	do {
		int n = recv(m_con,m_content+m_clen0,m_contlen-m_clen0,0);
		if (n == -1) {
			log_error(TAG,"error while downloading content: %s",strneterr(m_con));
			return;
		}
		m_clen0 += n;
	} while (m_contlen != m_clen0);
}


void HttpRequest::discardContent()
{
	size_t dsize = m_contlen - m_clen0;
	free(m_content);
	m_content = 0;
	m_clen0 = 0;
	size_t asize = 512 > dsize ? 512 : dsize;
	char *tmp = (char *)malloc(asize);
	assert(tmp);
	do {
		int n = recv(m_con,tmp,asize < dsize ? asize : dsize,0);
		if (n == -1) {
			log_error(TAG,"error while discarding: %s",strneterr(m_con));
			break;
		}
		dsize -= n;
	} while (dsize);
	free(tmp);
}


HttpRequest *HttpRequest::parseRequest(int con)
{
	size_t bs = 2048;
	char *buf = (char*) malloc(bs);
	int n = recv(con,buf,bs-1,0);
//	TimeDelta td(__FUNCTION__);
	if (n < 0) {
		log_error(TAG,"failed to receive data: %s",strneterr(con));
		free(buf);
		return 0;
	}
	if (n == 0) {
		log_info(TAG,"empty request");	// this is normal for certain types of requests
		n = recv(con,buf,bs-1,0);
		if (n <= 0) {
			log_error(TAG,"receive after empty: %d, %s",n,strneterr(con));
			free(buf);
			return 0;
		}
	}
	buf[bs-1] = 0;
	buf[n] = 0;
//	log_dbug(TAG,"http-req:");
//	con_write(buf,n);
	HttpRequest *r = new HttpRequest(con);
	char *cr = strchr(buf,'\r');
	if (cr == 0) {
		log_warn(TAG,"header line too long (%u)\n%-256s",n,buf);
		r->m_error = "header parser error";
		return r;
	}
	*cr = 0;
	//log_info(TAG,"header: '%s'",buf);
	char *uri = 0;
	if (0 == memcmp(buf,"GET ",4)) {
		r->m_httpreq = hq_get;
		uri = (char*)buf+4;
		char *q = strchr(uri,'?');
		if (q)
			r->parseArgs(q+1);
	} else if (0 == memcmp(buf,"PUT ",4)) {
		r->m_httpreq = hq_put;
		uri = buf+4;
	} else if (0 == memcmp(buf,"POST ",5)) {
		r->m_httpreq = hq_post;
		uri = buf+5;
	} else if (0 == memcmp(buf,"DELETE ",7)) {
		r->m_httpreq = hq_post;
		uri = buf+7;
	}
	char *at = uri;
	while ((at < buf+bs) && ((*at & 0x80) == 0) && (*at != ' '))
		++at;
	if ((at == buf+bs) || (*at != ' ')) {
		log_error(TAG,"invalid start line while parsing request:\n");
		delete r;
		free(buf);
		return 0;
	}
	r->m_URI = strndup(uri,at-uri);
	++at;
	if (0 == memcmp(at,"HTTP/1.0",9)) {
		r->m_httpver = hv_1_0;
		r->m_keepalive = false;
	} else if (0 == memcmp(at,"HTTP/1.1",9)) {
		r->m_httpver = hv_1_1;
	} else if (0 == memcmp(at,"HTTP/2.0",9)) {
		r->m_httpver = hv_2_0;
	} else {
		log_error(TAG,"unknown http version %8s",at);
		delete r;
		free(buf);
		return 0;
	}
	at += 10;
	while ((at != 0) && (*at != 0) && (*at != '\r')) {
		at = r->parseHeader(at,n-(at-buf));
	}
	if (*at == 0) {
		free(buf);
		return r;
	}
	if ((at[0] != '\r') || (at[1] != '\n')) {
		log_error(TAG,"invalid message termination");
		r->m_error = "invalid message termination";
		free(buf);
		return r;
	}
	at += 2;
	r->m_clen0 = n - ((char*)at-buf);
	r->m_content = (char*)malloc(r->m_clen0+1);
	memcpy(r->m_content,at,r->m_clen0);
	r->m_content[r->m_clen0] = 0;
	//log_info(TAG,"clen0 = %d",r->m_clen0);
	free(buf);
	return r;
}


void HttpRequest::setURI(const char *uri)
{
	if (m_URI)
		free(m_URI);
	m_URI = strdup(uri);
}



