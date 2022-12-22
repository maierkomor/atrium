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

#include "tcp_terminal.h"
#include "lwtcp.h"
#include "netsvc.h"
#include "log.h"

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


TcpTerminal::TcpTerminal(LwTcp *con, bool crnl)
: Terminal(crnl)
, m_con(con)
{

}


int TcpTerminal::write(const char *buf, size_t s)
{
	int n = m_con->write(buf,s);
	if (n < 0)
		m_error = m_con->error();
	else
		m_error = 0;
	return n;
}


int TcpTerminal::read(char *buf, size_t s, bool block)
{
	int n = m_con->read(buf, s, block ? portMAX_DELAY : 0);
	//log_info(logmod_lwtcp,"recv: %d",n);
	if (n < 0) {
		m_error = "unknown error";
	} else {
		m_error = 0;
	}
	return n;
}


int TcpTerminal::disconnect()
{
	return m_con->close();
}


void TcpTerminal::sync(bool block)
{
	m_con->sync(block);
}
