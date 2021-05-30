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

#include <sdkconfig.h>

#if defined CONFIG_LEDDISP

#include "binformats.h"
#include "display.h"
#include "globals.h"
#include "log.h"
#include "MAX7219.h"

static const char TAG[] = "disp";


int display_setup()
{
	if (HWConf.has_max7219()) {
		// 5V level adjustment necessary
		// ESP8266 is not capable of driving directly
		// ESP32 seems to work
		const Max7219Config &c = HWConf.max7219();
		if (c.has_clk() && c.has_dout() && c.has_cs() && c.has_digits()) {
			MAX7219Drv::create((gpio_num_t)c.clk(),(gpio_num_t)c.dout(),(gpio_num_t)c.cs(),c.odrain());
		}
	}
	if (HWConf.has_display()) {
		if (LedCluster *l = LedCluster::getInstance()) {
			const DisplayConfig &c = HWConf.display();
			new SegmentDisplay(l,(SegmentDisplay::addrmode_t)c.mode(),c.maxx(),c.maxy());
		} else {
			log_warn(TAG,"display configured, but no LED cluster available");
		}
	}
	return 0;
}

#endif
