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

#define EXEFLAG_ADMIN		1
#define EXEFLAG_INTERACTIVE	2

#ifdef __cplusplus
class Terminal;
int exe_flags(char *cmd);
int shellexe(Terminal &, char *cmd);
void shell(Terminal &term, bool prompt = true);
int help_cmd(Terminal &term, const char *arg);

extern "C"
#endif
const char *getpwd();


#endif
