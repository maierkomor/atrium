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

#ifndef MDNS_H
#define MDNS_H

#include <lwip/ip_addr.h>

#ifdef __cplusplus
extern "C" {
#endif

void mdns_setup();
void udns_setup();
int udns_query(const char *hn, ip_addr_t *ip, void (*cb)(const char *, const ip_addr_t *, void *), void *arg);
int udns_resolve(const char *hn, ip_addr_t *ip);
int udns_set_nameserver(unsigned, const char *);
int udns_add_nameserver(const char *ns);
int udns_set_maxcache(unsigned cs);
int udns_add_txt(const char *key, const char *value);
int udns_add_ptr(const char *p);
void udns_update_hostname();


#ifdef __cplusplus
}
#endif

#endif
