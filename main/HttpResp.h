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

#ifndef HTTP_RESP_H
#define HTTP_RESP_H

#include "estring.h"


/*
typedef enum {
	HTTP_OK = 200,
	HTTP_CREATED = 201,
	HTTP_ACCEPTED = 202,
	HTTP_NO_CONTENT = 204,
	HTTP_RESET_CONTENT = 205,
	HTTP_BAD_REQUEST = 400,
	HTTP_UNAUTHORIZED = 401,
	HTTP_FORBIDDEN = 402,
	HTTP_NOT_FOUND = 404,
	HTTP_URI_TOO_LONG = 414,
	HTTP_RC_LARGE_HDR = 431,
	HTTP_INTERNAL_ERROR = 500,
	HTTP_NOT_IMPLEMENTED = 501,
	HTTP_SVC_UNAVAIL = 503,
	HTTP_INSUF_SPACE = 507,
	HTTP_INSUFFICIENT_STORAGE = 507,
} http_err_t;
*/

extern const char CT_APP_JSON[];
extern const char CT_APP_OCTET_STREAM[];
extern const char CT_TEXT_PLAIN[];
extern const char CT_TEXT_HTML[];
extern const char CT_TEXT_CSV[];
extern const char CT_TEXT_CSS[];
extern const char CT_IMAGE_JPEG[];


extern const char HTTP_OK[];
extern const char HTTP_CREATED[];
extern const char HTTP_ACCEPT[];
extern const char HTTP_NO_CONT[];
extern const char HTTP_RST_CONT[];
extern const char HTTP_BAD_REQ[];
extern const char HTTP_UNAUTH[];
extern const char HTTP_FORBIDDEN[];
extern const char HTTP_NOT_FOUND[];
extern const char HTTP_TOO_LONG[];
extern const char HTTP_2MANY_REQ[];
extern const char HTTP_LARGE_HDR[];
extern const char HTTP_INTERNAL_ERR[];
extern const char HTTP_NOT_IMPL[];
extern const char HTTP_SVC_UNAVAIL[];
extern const char HTTP_INV_VERSION[];
extern const char HTTP_INSUF_SPACE[];
extern const char HTTP_NETAUTH_REQ[];

class LwTcp;


class HttpResponse
{
	public:
	explicit HttpResponse(const char *r = 0);

	void setContentType(const char *);
	void setContentLength(unsigned);
	void setResult(const char *r)
	{ m_result = (char*)r; }

	void addHeader(const char *);

	void addContent(const char *, size_t l = 0);
	void writeContent(const char *,  ...);

	bool senddata(LwTcp *, int fd = -1);

	estring &contentString()
	{ return m_content; }

	private:
	HttpResponse(const HttpResponse &);
	HttpResponse& operator = (const HttpResponse &);

	const char *m_result;
	estring m_header;
	estring m_content;
	int m_fd = -1;
	LwTcp *m_con = 0;
};

#endif
