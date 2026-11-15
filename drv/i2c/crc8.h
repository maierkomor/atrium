/*
 *  Copyright (C) 2026, Thomas Maier-Komor
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

#ifndef CRC_H
#define CRC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
uint8_t crc8_0x31(uint8_t *data, unsigned len, uint16_t crc = 0xff);
extern "C"
uint8_t crc8_0x31_2(uint8_t data0, uint8_t data1);
#else
uint8_t crc8_0x31(uint8_t *data, unsigned len, uint8_t crc);
uint8_t crc8_0x31_2(uint8_t data0, uint8_t data1);
#endif

#endif
