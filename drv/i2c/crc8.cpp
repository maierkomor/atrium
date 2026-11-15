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

#include "crc8.h"

/*
uint8_t calc_crc8(const uint8_t *data, size_t n)
{
	uint8_t crc = 0xff;
	for (unsigned b = 0; b < n; ++b) {
		crc ^= data[b];
		for (unsigned i = 8; i > 0; --i) {
			if (crc & 0x80)
				crc = (crc<<1)^31;
			else
				crc <<= 1;
		}
	}
	return crc;
}


uint8_t crc8_0x31(uint8_t *data, unsigned len, uint8_t crc)
{
	do {
		crc ^= *data++;
		for (int i = 0; i < 8; ++i) {
			uint8_t x = crc & 0x80;
			crc <<= 1;
			if (x)
				crc ^= 0x31;
		}
	} while (--len);
	return crc;
}
*/


extern "C"
uint8_t crc8_0x31(uint8_t *data, unsigned len, uint16_t crc)
{
	do {
		crc ^= *data++;
		for (int i = 0; i < 8; ++i) {
			crc <<= 1;
			if (crc&0x100)
				crc ^= 0x31;
		}
	} while (--len);
	return (uint8_t) crc & 0xff;
}


extern "C"
uint8_t crc8_0x31_2(uint8_t data0, uint8_t data1)
{
	uint8_t crc = 0xff;
	crc ^= data0;
	for (int i = 0; i < 8; ++i) {
		uint8_t x = crc & 0x80;
		crc <<= 1;
		if (x)
			crc ^= 0x31;
	}
	crc ^= data1;
	for (int i = 0; i < 8; ++i) {
		uint8_t x = crc & 0x80;
		crc <<= 1;
		if (x)
			crc ^= 0x31;
	}
	return crc;
}


