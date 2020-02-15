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

#ifndef BME280_H
#define BME280_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int bme280_init(unsigned, unsigned);
uint8_t bme280_status();
//int bme280_read(int32_t *t, uint32_t *p, uint32_t *h);
int bme280_read(double *t, double *h, double *p);

#ifdef __cplusplus
}
#endif

#endif
