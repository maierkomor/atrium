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

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "support.h"

#include <map>
#include <set>
#include <assert.h>

class HttpRequest;
class HttpResponse;


class HttpServer
{
	public:
	HttpServer(const char *wwwroot, const char *rootmap);
	virtual ~HttpServer();

	typedef void (*www_fun_t)(HttpRequest *r);

	void setUploadDir(const char *);
	void addDirectory(const char *);
	void addFile(const char *);
	void addFunction(const char *, www_fun_t f);
	void addMemory(const char *, const char *);
	virtual bool authorized(const char *, const char *)
	{ return false; }

	void handleConnection(int);

	private:
	HttpServer(const HttpServer &);
	HttpServer& operator = (const HttpServer &);

	bool runFile(HttpRequest *);
	bool runDirectory(HttpRequest *);
	bool runFunction(HttpRequest *);
	bool runMemory(HttpRequest *);
	void performGET(HttpRequest *);
	void performPOST(HttpRequest *);
	void performPUT(HttpRequest *);

	const char *m_wwwroot, *m_rootmap, *m_upload;
	std::set<const char *,CStrLess> m_files;
	std::set<const char *,SubstrLess> m_dirs;
	std::map<const char *,www_fun_t,CStrLess> m_functions;
	std::map<const char *,const char *,CStrLess> m_memfiles;
};



inline HttpServer::~HttpServer()
{

}


#endif
