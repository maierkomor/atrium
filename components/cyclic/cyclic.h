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

#ifndef CYCLIC_H
#define CYCLIC_H


#ifdef __cplusplus
extern "C" {
#endif

unsigned cyclic_execute();
int cyclic_add_task(const char *name, unsigned (*loop)(void *), void * = 0, unsigned = 0);
int cyclic_rm_task(const char *name);

#ifdef __cplusplus
}
#endif

#endif
