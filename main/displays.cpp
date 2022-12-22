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

#if defined CONFIG_DISPLAY

#include "display.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "MAX7219.h"
#include "pcf8574.h"
#include "hd44780u.h"
#include "ssd1306.h"
#include "ssd1309.h"

#define TAG MODULE_DISP


int display_setup()
{
	if (HWConf.has_max7219()) {
		// 5V level adjustment necessary
		// ESP8266 is not capable of driving directly
		// ESP32 seems to work
		const Max7219Config &c = HWConf.max7219();
		if (c.has_clk() && c.has_dout() && c.has_cs() && c.has_digits()) {
			MAX7219Drv::create((xio_t)c.clk(),(xio_t)c.dout(),(xio_t)c.cs(),c.odrain());
		}
	}
	if (HWConf.has_display()) {
		const DisplayConfig &c = HWConf.display();
		if (!c.has_type() || !c.has_maxx()) {
			log_warn(TAG,"incomplete config");
			return 1;
		}
		disp_t t = c.type();
		uint8_t maxx = c.maxx();
		uint8_t maxy = c.maxy();
		if (LedCluster *l = LedCluster::getInstance()) {
			new SegmentDisplay(l,(SegmentDisplay::addrmode_t)t,maxx,maxy);
#ifdef CONFIG_PCF8574
		} else if (t == dt_pcf8574_hd44780u) {
			PCF8574 *dev = PCF8574::getInstance();
			if (dev == 0) {
				log_warn(TAG,"no pcf8574 found");
				return 1;
			}
			HD44780U *hd = new HD44780U(dev,maxx,maxy);
			hd->init();
#endif
#ifdef CONFIG_SSD1306
		} else if (t == dt_ssd1306) {
			SSD1306 *dev = SSD1306::getInstance();
			if (dev == 0) {
				log_warn(TAG,"no ssd1306 found");
				return 1;
			}
			dev->init(maxx,maxy,c.options());
#endif
#ifdef CONFIG_SSD1309
		} else if (t == dt_ssd1309) {
			SSD1309 *dev = SSD1309::getInstance();
			if (dev == 0) {
				log_warn(TAG,"no ssd1306 found");
				return 1;
			}
			dev->init(maxx,maxy,c.options());
#endif
		} else {
			log_warn(TAG,"display configured, but no LED cluster available");
		}
	}
	return 0;
}

#endif
