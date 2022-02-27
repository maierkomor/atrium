/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#ifndef NATIVE_IO_H
#define NATIVE_IO_H

#include "xio.h"


int coreio_config(uint8_t num, xio_cfg_t cfg);
int coreio_lvl_get(uint8_t num);
int coreio_lvl_hi(uint8_t num);
int coreio_lvl_lo(uint8_t num);
int coreio_lvl_set(uint8_t num, xio_lvl_t l);
void coreio_register();


#endif
