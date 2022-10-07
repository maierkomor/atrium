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

#ifndef OTA_H
#define OTA_H

#include <stdbool.h>

#ifdef __cplusplus
class Terminal;

const char *perform_ota(Terminal &t, char *source, bool changeboot);
const char *update_romfs(Terminal &t, char *source);
const char *update_part(Terminal &t, char *source, const char *dest);
const char *http_download(Terminal &t, char *addr, const char *fn);
const char *boot(Terminal &t, int argc, const char *args[]);
#endif

#endif
