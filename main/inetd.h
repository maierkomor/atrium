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

#ifndef INETD_H
#define INETD_H


#ifdef __cplusplus
extern "C" {
#endif

typedef enum { m_sock = 0, m_tcp = 1, m_udp = 2, m_bcast = 3 } inet_mode_t;
struct LwTcp;

int listen_port(int port, inet_mode_t mode, void (*session)(LwTcp *), const char *basename, const char *service, unsigned prio, unsigned stack);

#ifdef __cplusplus
}
#endif


#endif

