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

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "modules.h"

#define LOG_MAXLEN 95

//#define MUTEX_ABORT_TIMEOUT portMAX_DELAY
#define MUTEX_ABORT_TIMEOUT 10000


#ifdef __cplusplus
#include <vector>

extern "C"
void abort_on_mutex(SemaphoreHandle_t mtx, const char *);

class Lock
{
	public:
	explicit Lock(SemaphoreHandle_t mtx, const char *usage = "<unknown>")
	: m_mtx(mtx)
	{
		if (pdTRUE != xSemaphoreTake(mtx,MUTEX_ABORT_TIMEOUT))
			abort_on_mutex(mtx,usage);
       	}

	~Lock()
	{
		if (pdTRUE != xSemaphoreGive(m_mtx))
			abort();
	}

	private:
	SemaphoreHandle_t m_mtx;
};


class MLock
{
	public:
	explicit MLock(SemaphoreHandle_t mtx, const char *usage = "<unknown>")
	: m_mtx(mtx)
	, m_usage(usage)
	{
		if (pdTRUE != xSemaphoreTake(mtx,MUTEX_ABORT_TIMEOUT))
			abort_on_mutex(mtx,usage);
		m_locked = true;
       	}

	~MLock()
	{
		if (m_locked && (pdTRUE != xSemaphoreGive(m_mtx)))
			abort_on_mutex(m_mtx,m_usage);
	}

	void lock()
	{
		assert(!m_locked);
		if (pdTRUE != xSemaphoreTake(m_mtx,MUTEX_ABORT_TIMEOUT))
			abort_on_mutex(m_mtx,m_usage);
		m_locked = true;
	}

	void unlock()
	{
		assert(m_locked);
		if (pdTRUE != xSemaphoreGive(m_mtx))
			abort_on_mutex(m_mtx,m_usage);
		m_locked = false;
	}

	private:
	SemaphoreHandle_t m_mtx;
	const char *m_usage;
	bool m_locked;
};


class RLock
{
	public:
	explicit RLock(SemaphoreHandle_t mtx, const char *usage = "<unset>")
	: m_mtx(mtx)
	{
		if (pdTRUE != xSemaphoreTakeRecursive(mtx,MUTEX_ABORT_TIMEOUT))
			abort_on_mutex(mtx,usage);
	}

	~RLock()
	{
		if (pdTRUE != xSemaphoreGiveRecursive(m_mtx))
			abort();
	}

	private:
	SemaphoreHandle_t m_mtx;
};

class TryLock
{
	public:
	TryLock(SemaphoreHandle_t mtx, unsigned ms)
	: m_mtx(mtx)
	, held(pdTRUE == xSemaphoreTake(mtx,ms/portTICK_PERIOD_MS))
	{ }

	~TryLock()
	{
		if (held)
			xSemaphoreGive(m_mtx);
	}

	bool locked() const
	{ return held; }

	void release()
	{
		if (held) {
			xSemaphoreGive(m_mtx);
			held = false;
		}
	}

	private:
	SemaphoreHandle_t m_mtx;
	bool held;
};

struct estring;
void log_module_print(class Terminal &t);
void log_module_enable(const std::vector<estring> &mods);

extern "C" {
#endif

typedef enum {
	ll_error, ll_warn, ll_info, ll_debug, ll_local
} log_level_t;

void abort_on_mutex(SemaphoreHandle_t mtx, const char *);
void log_setup();
void dmesg_to_uart(int8_t);
void log_set_uart(int8_t);
void writelog(const char *f, ...);

void con_print(const char *str);
void con_printf(const char *f, ...);
void con_write(const char *str, ssize_t l);

void log_fatal(logmod_t m, const char *f, ...);
void log_error(logmod_t m, const char *f, ...);
void log_warn(logmod_t m, const char *f, ...);
void log_info(logmod_t m, const char *f, ...);
void log_dbug(logmod_t m, const char *f, ...);
void log_hex(logmod_t m, const void *d, unsigned n, const char *f, ...);
void log_local(logmod_t m, const char *f, ...);
void log_gen(log_level_t, logmod_t m, const char *f, ...);

struct timeval;
void log_syslog(log_level_t lvl, logmod_t m, const char *msg, size_t ml, struct timeval *);

int log_module_disable(const char *m);
int log_module_enable(const char *m);
//int log_module_enabled(const char *m);
int log_module_enabled(logmod_t m);

void log_common(log_level_t l, logmod_t a, const char *f, va_list val);

static inline void log_if(int b, logmod_t m, const char *f, ...)
{
	if (b != 0) {
		va_list val;
		va_start(val,f);
		log_common(ll_info,m,f,val);
		va_end(val);
	}
}

#ifdef __cplusplus
}
#endif

#endif
