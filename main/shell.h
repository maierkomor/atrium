/*
 *  Copyright (C) 2017-2023, Thomas Maier-Komor
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

#ifndef SHELL_H
#define SHELL_H

#define EXEFLAG_ADMIN		1
#define EXEFLAG_INTERACTIVE	2

#ifdef __cplusplus
class Terminal;
int exe_flags(char *cmd);
const char *shellexe(Terminal &, char *cmd);
void shell(Terminal &term, bool prompt = true);
const char *help_cmd(Terminal &term, const char *arg);
void print_hex(Terminal &term, const uint8_t *b, size_t s, size_t off = 0);

extern "C"
#endif
const char *getpwd();

typedef enum cmd_ret_e {
	RET_OK = 0,
	RET_FAILED,
	RET_INV_NUMARG,
	RET_INV_ARG_1,
	RET_INV_ARG_2,
	RET_INV_ARG_3,
	RET_INV_ARG_4,
	RET_OOM,		// out of memory
	RET_PERM,		// access denied
	RET_ERRNO,
} cmd_ret_t;


#endif
