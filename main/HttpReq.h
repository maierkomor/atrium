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

#ifndef HTTP_REQ_H
#define HTTP_REQ_H

#include <stdint.h>
#include <sys/types.h>
#include <map>
#include <vector>

#include "estring.h"

class LwTcp;

typedef enum
{
	hq_none = 0,
	hq_get,
	hq_post,
	hq_put,
	hq_delete,
} httpreq_t;

typedef enum
{
	hv_none = 0,
	hv_1_0 = 1,
	hv_1_1 = 2,
	hv_2_0 = 3,
} httpver_t;


class HttpRequest
{
	public:
	static HttpRequest *parseRequest(LwTcp *,char*,size_t);
	~HttpRequest();

	size_t getContentLength() const
	{ return m_contlen; }
	void fillContent();
	void discardContent();
	char *getContent();
	httpver_t getVersion() const;

	size_t getAvailableLength() const
	{ return m_clen0; }

	httpreq_t getType() const
	{ return m_httpreq; }

	const char *getError() const
	{ return m_error; }

	const char *getURI() const
	{ return m_URI; }

	LwTcp *getConnection() const
	{ return m_con; }

	bool keepAlive() const
	{ return m_keepalive; }

	void setKeepAlive(bool a)
	{ m_keepalive = a; }

	size_t numArgs();
	const estring &argName(size_t) const;
	const estring &arg(size_t) const;
	const estring &arg(const char *) const;
	const estring &getHeader(const char *) const;
	void setURI(const char *);

	private:
	explicit HttpRequest(LwTcp *);
	HttpRequest(const HttpRequest &);
	HttpRequest &operator = (const HttpRequest &);
	char *parseHeader(char *,size_t);
	void parseField(char *at, char *end);
	void parseArgs(const char *);

	LwTcp *m_con;
	httpreq_t m_httpreq;
	httpver_t m_httpver;
	char *m_URI;
	size_t m_contlen;
	ssize_t m_clen0;
	char *m_content;
	const char *m_error;
	std::vector< std::pair<estring,estring> > m_args;
	std::map<estring,estring> m_headers;
	bool m_keepalive;
};

#endif
