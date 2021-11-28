/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
 *
 *  This source file belongs to Wire-Format-Compiler.
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

#include "estring.h"

#include <assert.h>
#include <stdlib.h>

#if 1
#define con_print(...)
#define con_printf(...)
#else
#include <rom/ets_sys.h>
#include <stdarg.h>
#include <stdio.h>
#define con_print log_x
#define con_printf log_x
#ifdef CONFIG_IDF_TARGET_ESP8266
#include <esp8266/uart_register.h>
void uart_put(char c)
{
	uint32_t fifo;
	do {
		fifo = READ_PERI_REG(UART_STATUS(CONFIG_CONSOLE_UART_NUM)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
	} while ((fifo >> UART_TXFIFO_CNT_S & UART_TXFIFO_CNT) >= 126);
	WRITE_PERI_REG(UART_FIFO(CONFIG_CONSOLE_UART_NUM) , c);
}
void log_str(size_t l, const char *s)
{
	while (l--)
		uart_put(*s++);
}
void log_x(const char *f, ...)
{
	static char buf[128];
	va_list val;
	va_start(val,f);
	int n = vsnprintf(buf,sizeof(buf),f,val);
	assert(n < sizeof(buf));
	va_end(val);
	log_str(n,buf);
	uart_put('\n');
}
#elif defined CONFIG_IDF_TARGET_ESP32
#include <driver/uart.h>
void log_x(const char *f, ...)
{
	static char buf[128];
	static bool initialized = false;
	if (!initialized) {
		uart_driver_install((uart_port_t) 0,UART_FIFO_LEN*2,UART_FIFO_LEN*2,0,0,0);
		uart_enable_tx_intr((uart_port_t)0,1,0);
		initialized = true;
	}
	va_list val;
	va_start(val,f);
	int n = vsnprintf(buf,sizeof(buf),f,val);
	assert(n < sizeof(buf));
	va_end(val);
	uart_tx_chars((uart_port_t) 0,buf,n);
	uart_tx_chars((uart_port_t) 0,"\n",1);
	vTaskDelay(50);
}
void log_str(size_t l, const char *s)
{
	uart_tx_chars((uart_port_t) 0,s,l);
	uart_tx_chars((uart_port_t) 0,"\n",1);
	vTaskDelay(50);
}
#endif
#endif

estring::estring()
: str(0) //: str((char*)malloc(8))
, len(0)
, alloc(0) //, alloc(8)
{
	con_print("estring() %p",this);
}


estring::estring(size_t l, char c)
: str((char*)malloc(l+1))
, len(l)
, alloc(l+1)
{
	con_printf("estring(%u,'\\0%02o')",l,c);
	memset(str,c,l);
}


estring::estring(const char *s)
: len(strlen(s))
{
	con_printf("estring(s) %p: \"%s\"",this,s);
	if (len) {
		str = strdup(s);
		alloc = len+1;
	} else {
		str = 0;
		alloc = 0;
	}
}


estring::estring(const char *s, size_t l)
: str(0)
, len(l)
, alloc(0)
{
	con_printf("estring('%.*s',%d) %p",l,s,l,this);
	if (l) {
		reserve(l);
		memcpy(str,s,l);
	}
}


estring::estring(const char *s, const char *e)
: str(0)
, alloc(0)
{
	con_printf("estring(s,e) %p %.*s",this,e-s,s);
	reserve(e-s);
	len = e-s;
	memcpy(str,s,len);
}


estring::estring(const estring &a)
: len(a.len)
, alloc(a.alloc)
{
	con_printf("estring(const estring &) %p<=%p %d:%.*s",this,&a,(int)a.len,(int)a.len,a.str);
	if (a.len) {
		str = (char*)malloc(a.alloc);
		memcpy(str,a.str,len);
	} else {
		str = 0;
	}
}


estring::estring(estring &&a)
: str(a.str)
, len(a.len)
, alloc(a.alloc)
{
	con_printf("estring(estring &&) %.*s",a.len,a.str);
	a.len = 0;
	a.str = 0;
	a.alloc = 0;
}


estring::~estring()
{
	con_printf("~estring() %d",alloc);
	if (str)
		free(str);
}


void estring::reserve(size_t ns)
{
	assert(ns <= 0xffff);
	if (ns < alloc)
		return;
	// must always allocate ns+1 byte at minimum for trailing \0
	alloc = (ns+16) & ~15;	// reserve ns + [1..16]
	con_printf("estring %p::reserve(%d) %d %d",this,ns,alloc,(int)len);
	if (str) {
		char *n = (char *)realloc(str,alloc);
		assert(n);
		str = n;
	} else {
		str = (char *)malloc(alloc);
	}
}


estring &estring::operator = (const estring &a)
{
	con_printf("operator =(const estring &) %p %.*s",this,a.len,a.str);
	if (a.len) {
		reserve(a.len);
		memcpy(str,a.str,a.len+1);
	}
	len = a.len;
	return *this;
}


estring &estring::operator += (const estring &a)
{
	if (a.len) {
		con_printf("operator += %p %d:%.*s",this,(int)a.len,(int)a.len,a.str);
		size_t nl = len + a.len;
		reserve(nl);
		if (a.str)
		memcpy(str+len,a.str,a.len+1);
		len = nl;
	}
	return *this;
}


/*
estring &estring::operator = (const char *s)
{
	len = 0;
	return operator += (s);
}
*/


estring &estring::operator += (const char *s)
{
	if (s) {
		size_t l = strlen(s);
		con_printf("operator += %p '%s' (%d)",this,s,l);
		if (l) {
			reserve(l+len);
			memcpy(str+len,s,l);
			len += l;
		}
	}
	return *this;
}


estring &estring::operator += (char c)
{
	con_printf("operator += '%c'",c);
	push_back(c);
	return *this;
}


/*
bool estring::operator == (const char *s) const
{
	con_printf("operator == %p '%.*s' == '%s'",this,(int)len,str,s);
	assert(s);
	assert(this);
	return (str == 0) ? (*s == 0) : (0 == strcmp(str,s));
}
*/


/*
const char *estring::c_str() const
{
	if (str == 0) {
		str = (char *) malloc(4);
//		alloc = 4;
	} else {
		assert(len < alloc);
	}
	con_printf("c_str %p %d:'%.*s'",this,(int)len,(int)len,str);
	str[len] = 0;
	return str;
}
*/


/*
void estring::clear()
{
//	con_printf("clear %p %.*s",this,len,str);
	con_printf("clear %p %d",this,(int)len);
	if (str)
		str[0] = 0;
	len = 0;
}
*/


void estring::push_back(char c)
{
	con_printf("push_back %p '%c'",this,c);
	if (len + 2 >= alloc)
		reserve(len+1);
	str[len] = c;
	++len;
}


void estring::assign(const char *m, size_t s)
{
	con_printf("assign(%.*s,%u) %p",s,m,s,this);
	if (s) {
		if (s > alloc)
			reserve(s);
		memcpy(str,m,s);
	}
	len = s;
	con_printf("assign %p = '%.*s'",this,(int)len,str);
}


void estring::append(const char *m, size_t s)
{
	con_printf("append(%.*s,%u) %p",s,m,s,this);
	if (s) {
		reserve(len+s);
		memcpy(str+len,m,s);
		len += s;
	}
}


void estring::resize(size_t s, char c)
{
	con_printf("resize %p %d",this,s);
	reserve(s);
	if (s > len)
		memset(str+len,c,s-len);
	len = s;
}
