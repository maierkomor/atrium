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

#ifndef NVM_H
#define NVM_H


int nvm_setup();
void nvm_store_u8(const char *, uint8_t);
uint8_t nvm_read_u8(const char *, uint8_t);
void nvm_store_u16(const char *, uint16_t);
uint16_t nvm_read_u16(const char *, uint16_t);
void nvm_store_u32(const char *, uint32_t);
uint32_t nvm_read_u32(const char *, uint32_t);
void nvm_store_float(const char *, float);
float nvm_read_float(const char *, float);
int nvm_write(const char *name, const uint8_t *buf, size_t s);
int nvm_read_blob(const char *name, uint8_t **buf, size_t *len);
int nvm_store_blob(const char *name, const uint8_t *buf, size_t len);
const char *nvm_copy_blob(const char *to, const char *from);
void nvm_erase_key(const char *name);
void nvm_erase_all();

#endif
