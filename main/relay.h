/*
 *  Copyright (C) 2017-2019, Thomas Maier-Komor
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

#ifndef RELAY_H
#define RELAY_H

#include <sdkconfig.h>

#ifdef CONFIG_RELAY
void relay_on();
void relay_off();
void relay_toggle();
bool relay_state();
#ifdef __cplusplus
extern "C"
#endif
void relay_setup();
#else
#define relay_on()
#define relay_off()
#define relay_toggle()
#define relay_state()
#define relay_setup()
#endif

#endif
