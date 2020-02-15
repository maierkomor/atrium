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

#include "support.h"
#include "tcp_terminal.h"
#include "wifi.h"

#include <esp_err.h>
#include <lwip/tcp.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#ifdef write
#undef write
#endif

#ifdef read
#undef read
#endif

#ifdef close
#undef close
#endif


TcpTerminal::TcpTerminal(int con, bool crnl)
: Terminal(crnl)
, m_con(con)
{

}


int TcpTerminal::write(const char *buf, size_t s)
{
	int n = send(m_con,buf,s,0);
	if (n < 0)
		m_error = strneterr(m_con);
	return n;
}


int TcpTerminal::read(char *buf, size_t s, bool block)
{
	int n = recv(m_con,buf,s, block ? 0 : MSG_DONTWAIT);
	if (n < 0)
		m_error = strneterr(m_con);
	return n;
}


void TcpTerminal::disconnect()
{
	lwip_close(m_con);
}
