/*
 *  Copyright (C) 2018-2019, Thomas Maier-Komor
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

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ll_error, ll_warn, ll_info, ll_debug
} log_level_t;

void log_setup();
void syslog_setup(void);
void writelog(const char *f, ...);
void dbug(const char *f, ...);

void con_print(const char *str);
void con_printf(const char *f, ...);
void con_write(const char *str,size_t l);

void log_fatal(const char *m, const char *f, ...);
void log_error(const char *m, const char *f, ...);
void log_warn(const char *m, const char *f, ...);
void log_info(const char *m, const char *f, ...);
void log_dbug(const char *m, const char *f, ...);

void log_common(log_level_t l, const char *a, const char *f, va_list val);

static inline void log_if(int b, const char *m, const char *f, ...)
{
	if (b == 0)
		return;
	va_list val;
	va_start(val,f);
	log_common(ll_info,m,f,val);
	va_end(val);
}

#ifdef __cplusplus
}
#endif

#endif
