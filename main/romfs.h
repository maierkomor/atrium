/*
 *  Copyright (C) 2019, Thomas Maier-Komor
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

#ifndef ROMFS_H
#define ROMFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t RomfsBaseAddr, RomfsSpace;

size_t romfs_num_entries();
size_t romfs_size(int);
size_t romfs_offset(int);
void romfs_getentry(const char *n, size_t *s, size_t *o);
int romfs_open(const char *n);
int romfs_read_at(int i, char *buf, size_t s, size_t o);
const char *romfs_name(int i);
void romfs_setup();

#ifdef __cplusplus
}
#endif

#endif
