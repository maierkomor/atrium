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

#include "log.h"
#include "lwtcp.h"
#include "netsvc.h"
#include "profiling.h"

#define TAG MODULE_LWTCP

#ifdef CONFIG_SOCKET_API
#include <lwip/sockets.h>

LwTcp::LwTcp(int con)
: m_con(con)
{
	m_flags = lwip_fcntl(con,F_GETFL,0);
}

int LwTcp::close()
{
	if (m_con == -1)
		return -1;
	return ::close(m_con);
}


int LwTcp::connect(const char *hn, uint16_t port, bool block)
{
	log_local(TAG,"connect %s:%d",hn,(int)port);
	ip_addr_t ip;
	if (err_t e = resolve_fqhn(hn,&ip)) {
//		m_err = e;
		return e;
	}
	return connect(&ip,port,block);
}


int LwTcp::connect(ip_addr_t *ip, uint16_t port, bool block)
{
	if (m_con != -1) {
		log_warn(TAG,"already connected");
		return ERR_USE;
	}
	int s = socket(AF_INET,SOCK_STREAM,0);
	if (-1 == s) {
		log_warn(TAG,"unable to create socket: %s",strerror(errno));
		return -1;
	}
	struct sockaddr_in in;
	struct sockaddr_in6 in6;
	struct sockaddr *a;
	size_t as;
	if (IP_IS_V6(ip)) {
		in6.sin6_family = AF_INET6;
		in6.sin6_port = htons(port);
		a = (struct sockaddr *) &in6;
		as = sizeof(struct sockaddr_in6);
		memcpy(&in6.sin6_addr,ip_2_ip6(ip),sizeof(in6));
	} else {
		in.sin_family = AF_INET;
		in.sin_port = htons(port);
		a = (struct sockaddr *) &in;
		as = sizeof(struct sockaddr_in);
		memcpy(&in.sin_addr,ip_2_ip4(ip),sizeof(in));
	}
	if (-1 == lwip_connect(s,a,as)) {
		log_warn(TAG,"connect to %s failed: %s\n",inet_ntoa(ip),strerror(errno));
		::close(s);
		return -1;
	}
	m_con = s;
	log_dbugx(TAG,"connected to %s",inet_ntoa(ip));
	return 0;
}


const char *LwTcp::error() const
{ return strerror(errno); }


int LwTcp::read(char *data, size_t l, unsigned timeout)
{
	if (m_con == -1)
		return -1;
	log_dbug(TAG,"read(%d,%u)",l,timeout);
	if (timeout) {
		if ((m_flags & O_NONBLOCK) != 0) {
			m_flags &= !O_NONBLOCK;
			int r = lwip_fcntl(m_con,F_SETFL,m_flags);
			log_dbug(TAG,"setfl block %d",r);
		}
		/*
		if (timeout != portMAX_DELAY) {
			int r;
			struct timeval tv;
			tv.tv_sec = timeout/1000;
			tv.tv_usec = (timeout - tv.tv_sec*1000)*1000;
			do {
				fd_set s;
				FD_SET(m_con,&s);
				r = select(1,&s,0,0,&tv);
				log_dbug(TAG,"select = %d",r);
			} while ((r == -1) && (errno == EINTR));
			if (r <= 0)
				return r;
		}
		*/
	} else {
		if ((m_flags & O_NONBLOCK) == 0) {
			m_flags |= O_NONBLOCK;
			int r = lwip_fcntl(m_con,F_SETFL,m_flags);
			log_dbug(TAG,"setfl non-block %d",r);
		}
	}
	int r = ::read(m_con,data,l);
	log_dbug(TAG,"read(%d,%u)=%d %d",l,timeout,r,errno);
	if ((r == -1) && (errno == EWOULDBLOCK))
		return 0;
	return r;
}

int LwTcp::write(const char *data, size_t l, bool copy_ignored)
{
	if (m_con == -1)
		return -1;
	return ::write(m_con,data,l);
}


///////////////////////////////////////////////////////////////////////////////
#else // ESP8266
///////////////////////////////////////////////////////////////////////////////

#include "lwip/inet.h"
//#include "inetd.h"
#include "udns.h"
#include "tcpio.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <lwip/tcpip.h>
#include <lwip/priv/tcpip_priv.h>

#include <string.h>

#define TCP_BLOCK_SIZE 1436
#ifdef CONFIG_IDF_TARGET_ESP32
#define MAX_BUF_SIZE (8*1024)
#else
#define MAX_BUF_SIZE (4*1024)
#endif

#if 0
#define log_devel log_local
#else
#define log_devel(...)
#endif


LwTcpListener *LwTcpListener::First = 0;


LwTcp::LwTcp(struct tcp_pcb *pcb)
: m_pcb(pcb)
, m_port(pcb->local_port)
{
	m_mtx = xSemaphoreCreateRecursiveMutex();
	m_sem = xSemaphoreCreateBinary();
	m_send = xSemaphoreCreateBinary();
#ifndef CONFIG_IDF_TARGET_ESP8266
	m_lwip = xSemaphoreCreateBinary();
#endif
	// LWIP_LOCK(); -- called from lwip context
	tcp_recv(pcb,handle_recv);
	tcp_sent(pcb,handle_sent);
	tcp_err(pcb,handle_err);
	tcp_arg(pcb,this);
	//LWIP_UNLOCK(); -- called from lwip context
	log_localx(TAG,"connected to %s:%u",inet_ntoa(pcb->remote_ip),(unsigned)pcb->remote_port);
}


LwTcp::LwTcp()
{
	m_mtx = xSemaphoreCreateRecursiveMutex();
	m_sem = xSemaphoreCreateBinary();
	m_send = xSemaphoreCreateBinary();
#ifndef CONFIG_IDF_TARGET_ESP8266
	m_lwip = xSemaphoreCreateBinary();
#endif
}


LwTcp::~LwTcp()
{
	log_local(TAG,"~%u: %u",m_port,m_total);
	if (m_pcb) 
		close();
	if (m_pbuf) {
#ifdef CONFIG_IDF_TARGET_ESP8266
		LWIP_LOCK();
		pbuf_free(m_pbuf);
		LWIP_UNLOCK();
#else
		tcp_pbuf_arg_t a;
		a.pbuf = m_pbuf;
		a.sem = m_lwip;
		tcpip_send_msg_wait_sem(tcp_pbuf_free_fn,&a,&m_lwip);
#endif
	}
	vSemaphoreDelete(m_mtx);
	vSemaphoreDelete(m_sem);
	vSemaphoreDelete(m_send);
}


#ifndef CONFIG_IDF_TARGET_ESP8266
void LwTcp::close_fn(void *arg)
{
	LwTcp *a = (LwTcp *)arg;
	tcp_err(a->m_pcb,0);
	tcp_recv(a->m_pcb,0);
	tcp_sent(a->m_pcb,0);
	a->m_err = tcp_close(a->m_pcb);
	xSemaphoreGive(a->m_lwip);
}
#endif


int LwTcp::close()
{
	if (m_pcb == 0)
		return -1;
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	tcp_err(m_pcb,0);
	tcp_recv(m_pcb,0);
	tcp_sent(m_pcb,0);
	err_t e = tcp_close(m_pcb);
	m_pcb = 0;
	LWIP_UNLOCK();
	m_err = e;
#else
	tcpip_send_msg_wait_sem(close_fn,this,&m_lwip);
	err_t e = m_err;
#endif
	if (e)
		log_local(TAG,"close error %d",e);
	return e;
}


int LwTcp::connect(const char *hn, uint16_t port, bool block)
{
	log_local(TAG,"connect %s:%d",hn,(int)port);
	ip_addr_t ip;
	if (err_t e = resolve_fqhn(hn,&ip)) {
		m_err = e;
		return e;
	}
	return connect(&ip,port,block);
}


#ifndef CONFIG_IDF_TARGET_ESP8266
void LwTcp::connect_fn(void *arg)
{
	LwTcp *a = (LwTcp *)arg;
	a->m_pcb = tcp_new();
	tcp_arg(a->m_pcb,arg);
	tcp_err(a->m_pcb,handle_err);
	a->m_err = tcp_connect(a->m_pcb,a->m_addr,a->m_port,handle_connect);
	xSemaphoreGive(a->m_lwip);
}
#endif


int LwTcp::connect(ip_addr_t *a, uint16_t port, bool block)
{
	if (m_pcb) {
		log_warn(TAG,"pcb already initialized");
		return ERR_USE;
	}
	m_port = port;
	log_localx(TAG,"connect ip %s:%d",inet_ntoa(a),(int)port);
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	m_pcb = tcp_new();
	tcp_arg(m_pcb,this);
	tcp_err(m_pcb,handle_err);
	m_err = tcp_connect(m_pcb,a,port,handle_connect);
	LWIP_UNLOCK();
#else
	m_addr = a;
	tcpip_send_msg_wait_sem(connect_fn,this,&m_lwip);
#endif
	if (m_err != 0) {
		log_warn(TAG,"connect: %d",m_err);
		return -1;
	}
	if (block) {
		xSemaphoreTake(m_sem,portMAX_DELAY);
		log_local(TAG,"connect @%u",port);
	}
	return 0;
}


bool LwTcp::isConnected() const
{
	return (m_pcb && (m_pcb->state == ESTABLISHED));
}


void LwTcp::handle_err(void *arg, err_t e)
{
	if (arg == 0) {
		log_warn(TAG,"error@<null> %u",e);
		return;
	}
	LwTcp *P = (LwTcp *)arg;
	if (e == ERR_ISCONN) {
		assert(P->m_pcb);
		log_warn(TAG,"already connected to %s:%u",e,inet_ntoa(P->m_pcb->remote_ip),P->m_port);
	} else {
		log_warn(TAG,"error@%u %d",P->m_port,e);
		P->m_err = e;
		P->m_pcb = 0;
	}
	if (pdFALSE == xSemaphoreGive(P->m_sem))
		log_local(TAG,"m_sem");
	if (pdFALSE == xSemaphoreGive(P->m_send))
		log_local(TAG,"m_send");
}


err_t LwTcp::handle_sent(void *arg, struct tcp_pcb *pcb, u16_t l)
{
	PROFILE_FUNCTION();
	LwTcp *P = (LwTcp *)arg;
	log_devel(TAG,"sent@%u %u/%u",pcb->local_port,l,P->m_nwrite);
	RLock lock(P->m_mtx);
	assert(l <= P->m_nwrite);
	P->m_nwrite -= l;
	if (P->m_nwrite == 0)
		if (pdFALSE == xSemaphoreGive(P->m_send))
			log_local(TAG,"@%u m_send already set",P->m_port);
	return 0;
}


err_t LwTcp::handle_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pbuf, err_t e)
{
	PROFILE_FUNCTION();
	log_local(TAG,"recv %u err=%d",pbuf ? pbuf->tot_len : 0,e);
	LwTcp *P = (LwTcp *)arg;
	assert(pcb);
	if ((e == 0) && (pbuf != 0)) {
		int r = 0;
		RLock lock(P->m_mtx);
		bool give = true;
		unsigned recved = pbuf->tot_len;
		if ((P->m_buf != 0) && (P->m_pbuf == 0)) {
			unsigned copy = (P->m_bufsize-P->m_fill >= pbuf->tot_len) ? pbuf->tot_len : P->m_bufsize-P->m_fill;
			pbuf_copy_partial(pbuf,P->m_buf+P->m_fill,copy,0);
			log_devel(TAG,"recv@%u direct %u/%u to %u",P->m_port,copy,pbuf->tot_len,P->m_fill);
			P->m_fill += copy;
			if (copy == pbuf->tot_len) {
				pbuf_free(pbuf);
				pbuf = 0;
				copy = 0;
			}
			while (pbuf && (copy >= pbuf->len)) {
				struct pbuf *d = pbuf;
				copy -= d->len;
				pbuf = pbuf->next;
				log_devel(TAG,"recv free %u",d->len);
				pbuf_ref(pbuf);
				pbuf_free(d);
			}
			P->m_pbuf = pbuf;
			P->m_taken = copy;
			log_devel(TAG,"taken=%u,fill=%u,pbuf=%u",P->m_taken,P->m_fill,P->m_pbuf?P->m_pbuf->tot_len:0);
			assert(P->m_fill <= P->m_bufsize);
		} else if (0 == P->m_pbuf) {
			log_devel(TAG,"recv@%u %u, pass pbuf",P->m_port,pbuf->tot_len);
			P->m_pbuf = pbuf;
			P->m_taken = 0;
		} else if (MAX_BUF_SIZE > P->m_pbuf->tot_len+pbuf->tot_len) {
			pbuf_cat(P->m_pbuf,pbuf);
			log_devel(TAG,"concat %u@%u => %u",P->m_pbuf->tot_len,P->m_port,P->m_pbuf->tot_len);
		} else {
			log_devel(TAG,"recv %u@%u, ERR_MEM",pbuf->tot_len,pcb->local_port);
			give = false;
			recved = 0;
			r = ERR_MEM;
		}
		if (recved) {
			log_devel(TAG,"recved@%u %u",P->m_port,recved);
			tcp_recved(pcb,recved);
		}
		if (give) {
			if (pdFALSE == xSemaphoreGive(P->m_sem)) {
				log_local(TAG,"m_sem already given");
			} else {
				log_devel(TAG,"recv give");
			}
		}
		return r;
	} else if (e != 0) {
		log_local(TAG,"recv error %d",e);
		if (pbuf) {
			tcp_recved(pcb, pbuf->tot_len);
			pbuf_free(pbuf);
		}
	} else if (pbuf == 0) {
		log_local(TAG,"recv@%u pbuf=0/FIN",pcb->local_port);
		tcp_close(pcb);
		P->m_pcb = 0;
		xSemaphoreGive(P->m_sem);
	} else {
		abort();
	}
	return 0;
}


err_t LwTcp::handle_connect(void *arg, struct tcp_pcb *pcb, err_t x)
{
	PROFILE_FUNCTION();
	LwTcp *P = (LwTcp *) arg;
	assert(x == ERR_OK);	// documented in LwIP to be always like this
	log_localx(TAG,"connection %s to %u",inet_ntoa(pcb->remote_ip),pcb->local_port);
	tcp_recv(pcb,handle_recv);
	tcp_sent(pcb,handle_sent);
	if (pdTRUE == xSemaphoreGive(P->m_sem))
		log_local(TAG,"connect give");
	else
		log_error(TAG,"semaphore");
	return 0;
}


int LwTcp::read(char *buf, size_t l, unsigned timeout)
{
	PROFILE_FUNCTION();
	int r;
	struct pbuf *tofree = 0;
	if ((m_pcb == 0) && (m_pbuf == 0)) {
		log_local(TAG,"read@%u(%u,%u)=0",m_port,l,timeout);
		return 0;
	}
	log_local(TAG,"read@%u(%u,%u) fill=%u,pbuf=%u",m_port,l,timeout,m_fill,m_pbuf?m_pbuf->tot_len:0);
	if (pdTRUE != xSemaphoreTakeRecursive(m_mtx,MUTEX_ABORT_TIMEOUT))
		abort_on_mutex(m_mtx,__FUNCTION__);
	if (m_pbuf) {
		unsigned avail = m_pbuf->tot_len - m_taken;
		unsigned copy = l > avail ? avail : l;
		log_devel(TAG,"avail %u, copy %u, taken %u",avail,copy,m_taken);
		assert(m_taken+copy <= m_pbuf->tot_len);
		pbuf_copy_partial(m_pbuf,buf,copy,m_taken);
		m_taken += copy;
		if (m_taken == m_pbuf->tot_len) {
			log_local(TAG,"total free",m_taken);
			tofree = m_pbuf;
			m_pbuf = 0;
			m_taken = 0;
		} else if (m_pbuf->len <= m_taken) {
			tofree = m_pbuf;
			do {
				m_taken -= m_pbuf->len;
				m_pbuf = m_pbuf->next;
			} while (m_pbuf->len <= m_taken); 
			pbuf_ref(m_pbuf);
		}
		r = copy;
	} else if (timeout == 0) {
		log_devel(TAG,"non-blocking");
		r = 0;	// would block
	} else {
		assert(m_taken == 0);
		m_buf = buf;
		m_bufsize = l;
		m_fill = 0;
		while ((m_fill == 0) && (m_pcb != 0) && (m_pcb->state <= ESTABLISHED)) {
			xSemaphoreGiveRecursive(m_mtx);
			log_local(TAG,"wait recv on %u",m_port);
			if (pdTRUE != xSemaphoreTake(m_sem,timeout)) {
				log_local(TAG,"read@%u: timeout",m_port);
				return 0;
			}
			if (pdTRUE != xSemaphoreTakeRecursive(m_mtx,MUTEX_ABORT_TIMEOUT))
				abort_on_mutex(m_mtx,__FUNCTION__);
		}
		log_devel(TAG,"::read fill=%u,pbuf=%u",m_fill,m_pbuf?m_pbuf->tot_len:0);
		if (m_fill) {
			r = m_fill;
			assert(m_fill <= m_bufsize);
			m_fill = 0;
			m_buf = 0;
		} else if ((m_pcb == 0) || (m_pcb->state > ESTABLISHED) || (m_pcb->state == CLOSED)) {
			r = -1;
		} else {
			con_printf("state %d",m_pcb->state);
			abort();
		}
	}
	xSemaphoreGiveRecursive(m_mtx);
	if (tofree) {
#ifdef CONFIG_IDF_TARGET_ESP8266
		LWIP_LOCK();
		pbuf_free(tofree);
		LWIP_UNLOCK();
#else
		tcp_pbuf_arg_t a;
		a.pbuf = tofree;
		a.sem = m_lwip;
		tcpip_send_msg_wait_sem(tcp_pbuf_free_fn,&a,&m_lwip);
#endif
	}
	log_local(TAG,"read@%u(%u)=%d",m_port,l,r);
	if (r > 0)
		m_total += r;
	return r;
}


int LwTcp::send(const char *buf, size_t l, bool copy)
{
	if (l == 0)
		return 0;
	PROFILE_FUNCTION();
	log_devel(TAG,"send@%u %u %scopy",m_port,l,copy?"":"no-");
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	err_t e = tcp_write(m_pcb,buf,l,copy ? TCP_WRITE_FLAG_COPY : 0);
	LWIP_UNLOCK();
#else
	tcpwrite_arg_t a;
	a.pcb = m_pcb;
	a.data = buf;
	a.size = l;
	a.sem = m_lwip;
	a.name = "send";
	tcpip_send_msg_wait_sem(tcpwrite_fn,&a,&m_lwip);
	err_t e = a.err;
#endif
	if (e == 0) {
		log_devel(TAG,"@%u nwrite=%u",m_port,m_nwrite);
		RLock lock(m_mtx);
		m_nwrite += l;
		m_nout += l;
	} else {
		log_warn(TAG,"@%u error %d",m_port,e);
	}
	return e;
}


int LwTcp::write(const char *buf, size_t l, bool copy)
{
	PROFILE_FUNCTION();
	int e;
	for (;;) {
		e = send(buf,l,copy);
		if (e != -1) {
			m_err = e;
			return e;
		}
		log_local(TAG,"send=-1");
		sync(false);
		vTaskDelay(10);
	}
	return 0;
}


void LwTcp::sync(bool block)
{
	PROFILE_FUNCTION();
	log_local(TAG,"@%u sync %d",m_port,block);
	err_t r = 0;
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	unsigned w = m_nwrite;
	if (m_nout) {
		log_devel(TAG,"@%u sync %d output %u",m_port,block,m_nout);
		r = tcp_output(m_pcb);
		m_nout = 0;
	}
	LWIP_UNLOCK();
#else
	unsigned w = m_nwrite;
	if (m_nout) {
		tcpout_arg_t a;
		a.pcb = m_pcb;
		a.sem = m_lwip;
		a.name = "sync";
		tcpip_send_msg_wait_sem(tcpout_fn,&a,&m_lwip);
		m_nout = 0;
		r = a.err;
	}
#endif
	if (r) {
		log_warn(TAG,"output@%u=%d",m_port,r);
	} else {
		log_devel(TAG,"sync@%u %u %s",m_port,w,block?"block":"quick");
	}
	if (block && w) {
		if (pdTRUE != xSemaphoreTake(m_send,portMAX_DELAY))
			abort_on_mutex(m_send,__FUNCTION__);
		log_devel(TAG,"@%u synced",m_port);
	}
}


LwTcpListener::LwTcpListener(uint16_t port, void (*session)(LwTcp *), const char *name, uint16_t stack, uint8_t prio)
: m_next(First)
, m_name(name)
, m_session(session)
, m_port(port)
, m_stack(stack)
, m_prio(prio)
{
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	m_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
	if (err_t e = tcp_bind(m_pcb,IP_ADDR_ANY,port)) {
		log_warn(TAG,"binding to port %d: %d",(int)port,e);
	} else {
		m_pcb = tcp_listen(m_pcb);
		tcp_arg(m_pcb,this);
		tcp_accept(m_pcb,handle_accept);
	}
	LWIP_UNLOCK();
#else
	m_lwip = xSemaphoreCreateBinary();
	tcpip_send_msg_wait_sem(create_fn,this,&m_lwip);
#endif
	/*
	char svc[strlen(name)+7];
	svc[0] = '_';
	strcpy(svc+1,name);
	strcat(svc,"._tcp");
	udns_add_ptr(svc);
	*/
	log_local(TAG,"listening on port %u",(unsigned) port);
	First = this;
}


#ifndef CONFIG_IDF_TARGET_ESP8266
void LwTcpListener::create_fn(void *arg)
{
	LwTcpListener *a = (LwTcpListener *) arg;
	a->m_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
	if (err_t e = tcp_bind(a->m_pcb,IP_ADDR_ANY,a->m_port)) {
		log_warn(TAG,"binding to port %d: %d",(int)a->m_port,e);
	} else {
		a->m_pcb = tcp_listen(a->m_pcb);
		tcp_arg(a->m_pcb,a);
		tcp_accept(a->m_pcb,handle_accept);
	}
	xSemaphoreGive(a->m_lwip);
}


void LwTcpListener::abort_fn(void *arg)
{
	LwTcp *a = (LwTcp *) arg;
	tcp_abort(a->m_pcb);
	xSemaphoreGive(a->m_lwip);
}
#endif


LwTcpListener::~LwTcpListener()
{
#ifdef CONFIG_IDF_TARGET_ESP8266
	LWIP_LOCK();
	tcp_abort(m_pcb);
	LWIP_UNLOCK();
#else
	tcpip_send_msg_wait_sem(abort_fn,0,&m_lwip);
#endif
}


err_t LwTcpListener::handle_accept(void *arg, struct tcp_pcb *pcb, err_t x)
{
	if (x) {
		log_warn(TAG,"accept error %d",x);
	} else {
		LwTcpListener *P = (LwTcpListener *) arg;
		if (!P->m_enabled) {
			log_localx(TAG,"%s disabled: rejecting %s:%u",P->m_name,inet_ntoa(pcb->remote_ip),(int)pcb->remote_port);
			return 0;
		}
		LwTcp *N = new LwTcp(pcb);
		char name[24];
		sprintf(name,"%s%u",P->m_name,(unsigned)++P->m_id);
		BaseType_t r = xTaskCreatePinnedToCore((void (*)(void*))P->m_session,name,P->m_stack,(void*) N,P->m_prio,NULL,APP_CPU_NUM);
		if (r != pdTRUE) {
			log_warn(TAG,"cannot create session %s",name);
			return 1;
		}
		log_localx(TAG,"sevice %s for %s:%d",P->m_name,inet_ntoa(pcb->remote_ip),(int)pcb->remote_port);
	}
	return 0;
}


extern "C" 
int listen_port(int port, int mode, void (*session)(LwTcp*), const char *basename, const char *service, unsigned prio, unsigned stack)
{
//	assert(mode == m_tcp);
	LwTcpListener *server = new LwTcpListener(port,session,basename,stack,prio);
	return (server == 0);
}

#endif
