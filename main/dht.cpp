/*
 *  Copyright (C) 2018-2024, Thomas Maier-Komor
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

#include <sdkconfig.h>

#ifdef CONFIG_DHT

#include "dht.h"
#include "dhtdrv.h"
#include "globals.h"
#include "hwcfg.h"
#include "env.h"

#include <driver/gpio.h>

#define TAG MODULE_DHT


void dht_setup()
{
	const DhtConfig &c = HWConf.dht();
	if (c.has_gpio() && c.has_model()) {
		DHT *dev = new DHT;
		if (0 == dev->init(c.gpio(), c.model())) {
			dev->attach(RTData);
		} else {
			delete dev;
		}
	}
}


#endif
