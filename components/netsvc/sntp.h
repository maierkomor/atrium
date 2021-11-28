/*
 *  Copyright (C) 2021, Thomas Maier-Komor
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

#ifndef SNTP_H
#define SNTP_H

#ifdef __cplusplus
extern "C" {
#endif

int sntp_set_server(const char *server);
void sntp_set_interval(unsigned itv_ms);
void sntp_bc_init(void);
void sntp_mc_init(void);
int64_t sntp_last_update(void);

#ifdef __cplusplus
}
#endif
#endif
