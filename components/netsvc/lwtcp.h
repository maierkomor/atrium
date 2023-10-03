/*
 *  Copyright (C) 2021, Thomas Maier-Komor
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

#ifndef LWTCP_H
#define LWTCP_H

#include <sdkconfig.h>

#include <lwip/tcp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef LWIP_TCPIP_CORE_LOCKING
#error LWIP_TCPIP_CORE_LOCKING not defined
#endif


extern "C" const char *strlwiperr(int e);

class LwTcp
{
	public:
	LwTcp();
	~LwTcp();
	
	int connect(const char *hn, uint16_t port, bool block = true);
	int connect(ip_addr_t *a, uint16_t port, bool block = true);
	bool isConnected() const;
	int write(const char *data, size_t l, bool = true);
	int read(char *data, size_t l, unsigned timeout = portMAX_DELAY);
	void sync(bool = true);
	int close();

	const char *error() const
	{ return strlwiperr(m_err); }

	void setSync(bool s)
	{ m_sync = s; }

	err_t getError() const
	{ return m_err; }
	
	private:
	LwTcp(struct tcp_pcb *);
	LwTcp(const LwTcp &);
	LwTcp &operator = (const LwTcp &);
	int send(const char *data, size_t l, bool = true);

	static err_t handle_connect(void *arg, struct tcp_pcb *pcb, err_t x);
	static void handle_err(void *arg, err_t e);
	static err_t handle_sent(void *arg, struct tcp_pcb *pcb, u16_t l);
	static err_t handle_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pbuf, err_t e);
	static void connect_fn(void *);
	static void close_fn(void *);

	struct tcp_pcb *m_pcb = 0;
	char *m_buf = 0;
	struct pbuf *m_pbuf = 0;
	unsigned m_total = 0;
	uint16_t m_port = 0, m_bufsize = 0, m_fill = 0, m_taken = 0, m_nwrite = 0, m_nout = 0;
	err_t m_err = 0;
	SemaphoreHandle_t m_sem, m_send, m_mtx;
#if LWIP_TCPIP_CORE_LOCKING == 0
	ip_addr_t *m_addr = 0;
	SemaphoreHandle_t m_lwip;
#endif
	bool m_sync = true;

	friend class LwTcpListener;
};


class LwTcpListener
{
	public:
	LwTcpListener(uint16_t port, void (*)(LwTcp *), const char *, uint16_t, uint8_t);
	~LwTcpListener();

	static LwTcpListener *getFirst()
	{ return First; }
	
	LwTcpListener *getNext()
	{ return m_next; }

	void enable(bool e)
	{ m_enabled = e; }

	bool isEnabled() const
	{ return m_enabled; }

	const char *getName() const
	{ return m_name; }

	uint16_t getPort() const
	{ return m_port; }
	
	private:
	LwTcpListener(const LwTcp &);
	LwTcpListener &operator = (const LwTcpListener &);

	static err_t handle_accept(void *arg, struct tcp_pcb *pcb, err_t x);
	static void handle_err(void *arg, err_t e);
	static void abort_fn(void *);
	static void create_fn(void *);

	static LwTcpListener *First;
	LwTcpListener *m_next = 0;
	struct tcp_pcb *m_pcb = 0;
	const char *m_name = 0;
	void (*m_session)(LwTcp *) = 0;
	uint16_t m_port = 0, m_id = 0, m_stack;
	uint8_t m_prio;
	bool m_enabled = true;
#if LWIP_TCPIP_CORE_LOCKING == 0
	SemaphoreHandle_t m_lwip;
#endif
};

#endif
