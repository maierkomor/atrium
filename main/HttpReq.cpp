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

#include "HttpReq.h"

#include "log.h"
#include "lwtcp.h"
#include "netsvc.h"
#include "support.h"
#include "profiling.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef read
#undef read
#endif

#ifdef write
#undef write
#endif

using namespace std;

#define TAG MODULE_HTTP
static const estring Empty;


HttpRequest::HttpRequest(LwTcp *con)
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


static char *parsePercent(char *at, estring &value)
{
	assert(at[0] == '%');
	if (at[1] != 'x')
		return 0;
	uint8_t x;
	if ((at[2] >= '0') && (at[2] <= '9'))
		x = (at[2]-'0') << 4;
	else if ((at[2] >= 'A') && (at[2] <= 'F'))
		x = (at[2]-'A'+10) << 4;
	else
		return 0;
	if ((at[3] >= '0') && (at[3] <= '9'))
		x |= (at[3]-'0');
	else if ((at[3] >= 'A') && (at[3] <= 'F'))
		x |= (at[3]-'A'+10);
	else
		return 0;
	value += x;
	return at + 4;
}


void HttpRequest::parseField(char *at, char *end)
{
	PROFILE_FUNCTION();
	char fn[128], *f = fn;
	char c = *at;
	// parse field name
	while (c != ':') {
		if ((f-fn) == sizeof(fn)) {
			m_error = "field too big";
			return;
			}
		if (isTCHAR(c)) {
			*f++ = c;
		} else {
			m_error = "invalid character";
			return;
		}
		if (++at == end) {
			m_error = "missing value";
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
	estring value;
	if (*at == '"') {
		if (end[-1] != '"') {
			m_error = "missing terminating quote in header";
			return;
		}
		--end;
		*end = 0;
		++at;
	}
	if (end[-1] == '\\') {
		m_error = "trailing backslash in header";
		return;
	}
	log_dbug(TAG,"at = %s",at);
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
	log_dbug(TAG,"add pair '%s' -> '%s'",fn,value.c_str());
	m_headers.emplace(pair<estring,estring>(fn,value.c_str()));
	//auto x = m_headers.emplace(pair<estring,estring>(fn,value.c_str()));
	//log_dbug(TAG,"added pair '%s' -> '%s'",x.first->first.c_str(),x.first->second.c_str());
}


char *HttpRequest::parseHeader(char *at, size_t s)
{
	PROFILE_FUNCTION();
	char *cr = (char*)memchr(at,'\r',s);
	if (cr[1] != '\n') {
		m_error = "stray CR";
		return 0;
	}
	if (0 == strncasecmp(at,"Content-Length:",15)) {
		errno = 0;
		long l = strtol(at+15,0,0);
		if (errno != 0) {
			m_error = "invalid content-length";
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


const estring &HttpRequest::getHeader(const char *n) const
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


const estring &HttpRequest::arg(size_t i) const
{
	if (i >= m_args.size())
		return Empty;
	return m_args[i].second;
}


const estring &HttpRequest::arg(const char *a) const
{
	for (size_t i = 0, e = m_args.size(); i < e; ++i) {
		if (0 == strcmp(a,m_args[i].first.c_str()))
			return m_args[i].second;
	}
	return Empty;
}


const estring &HttpRequest::argName(size_t i) const
{
	if (i >= m_args.size())
		return Empty;
	return m_args[i].first;
}


void HttpRequest::parseArgs(const char *str)
{
	//PROFILE_FUNCTION();
	while (str) {
		const char *e = strchr(str,'=');
		const char *a = e ? strchr(e+1,'&') : 0;
		if (e && a) {
			m_args.push_back(make_pair(estring(str,e),estring(e+1,a)));
			log_dbug(TAG,"add arg %s",m_args.back().second.c_str());
			str = a + 1;
		} else if (e) {
			m_args.push_back(make_pair(estring(str,e),estring(e+1)));
			log_dbug(TAG,"add larg %s",m_args.back().second.c_str());
			str = 0;
		} else
			str = 0;
	}
}


char *HttpRequest::getContent()
{
	if (m_content == 0) {
		log_warn(TAG,"request to query discarded content");
		assert(0);
	}
	m_content[m_contlen] = 0;
	return m_content;
}


void HttpRequest::fillContent()
{
	PROFILE_FUNCTION();
	m_content = (char *)realloc(m_content,m_contlen+1);
	m_content[m_contlen] = 0;
	if (m_content == 0) {
		log_error(TAG,"Out of memory.");
		m_clen0 = 0;
		return;
	}
	do {
		int n = m_con->read(m_content+m_clen0,m_contlen-m_clen0);
		if (n == -1) {
			log_error(TAG,"error downloading: %s",m_con->error());
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
		int n = m_con->read(tmp,asize < dsize ? asize : dsize);
		if (n == -1) {
			log_error(TAG,"error discarding: %s",m_con->error());
			break;
		}
		dsize -= n;
	} while (dsize);
	free(tmp);
}


HttpRequest *HttpRequest::parseRequest(LwTcp *con, char *buf, size_t bs)
{
	/*
	int n, retry = 3;
	do {
		n = recv(con,buf,bs-1,0);
		log_dbug(TAG,"recv: %d, retry=%d",n,retry);
	} while ((n < 0) && --retry);
	*/
	int n = con->read(buf,bs-1,10000);
	if (n < 0) {
		log_warn(TAG,"receive: %s",con->error());
		return 0;
	}
	PROFILE_FUNCTION();
	if (n == 0) {
		log_dbug(TAG,"empty request");	// this is normal for certain types of requests
		n = con->read(buf,bs-1,1000);
		if (n <= 0) {
			log_error(TAG,"receive after empty: %d, %s",n,con->error());
			return 0;
		}
	}
	buf[n] = 0;
//	log_info(TAG,"request has %u bytes",n);
	log_dbug(TAG,"http-req: %*s",n,buf);
//	con_write(buf,n);
	char *cr = strchr(buf,'\r');
	if (cr == 0) {
		log_warn(TAG,"unterminated header (%u)\n%-256s",n,buf);
		return 0;
	}
	HttpRequest *r = new HttpRequest(con);
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
		log_warn(TAG,"invalid start line:\n");
		delete r;
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
		log_warn(TAG,"unknown http version %8s",at);
		delete r;
		return 0;
	}
	at += 10;
	while ((at != 0) && (*at != 0) && (*at != '\r')) {
		at = r->parseHeader(at,n-(at-buf));
	}
	if (*at == 0)
		return r;
	if ((at[0] != '\r') || (at[1] != '\n')) {
		log_warn(TAG,"invalid termination");
		r->m_error = "invalid termination";
		return r;
	}
	at += 2;
	r->m_clen0 = n - ((char*)at-buf);
	r->m_content = (char*)malloc(r->m_clen0+1);
	memcpy(r->m_content,at,r->m_clen0);
	r->m_content[r->m_clen0] = 0;
	//log_info(TAG,"clen0 = %d",r->m_clen0);
	return r;
}


void HttpRequest::setURI(const char *uri)
{
	if (m_URI)
		free(m_URI);
	m_URI = strdup(uri);
}



