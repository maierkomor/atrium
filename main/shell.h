/*
 *  Copyright (C) 2017-2021, Thomas Maier-Komor
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


#ifdef __cplusplus
class Terminal;
int shellexe(Terminal &, const char *cmd);
void shell(Terminal &term);
int arg_missing(Terminal &t);
int arg_invalid(Terminal &t, const char *a);
int arg_invnum(Terminal &t);
int arg_priv(Terminal &t);
int help_cmd(Terminal &term, const char *arg);

extern "C"
#endif
const char *getpwd();


#endif
