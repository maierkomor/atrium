/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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
#include "ili9341.h"
#include "MAX7219.h"
#include "pcf8574.h"
#include "hd44780u.h"
#include "sh1106.h"
#include "ssd1306.h"
#include "ssd1309.h"
#include "swcfg.h"

#define TAG MODULE_DISP


void display_setup()
{
#ifdef CONFIG_MAX7219
	if (HWConf.has_max7219()) {
		// 5V level adjustment necessary
		// ESP8266 is not capable of driving directly
		// ESP32 seems to work
		const Max7219Config &c = HWConf.max7219();
		if (c.has_clk() && c.has_dout() && c.has_cs() && c.has_digits()) {
			MAX7219Drv::create((xio_t)c.clk(),(xio_t)c.dout(),(xio_t)c.cs(),c.odrain());
		}
	}
#endif
	if (HWConf.has_display()) {
		const DisplayConfig &c = HWConf.display();
		if (!c.has_type() || !c.has_maxx()) {
			log_warn(TAG,"incomplete config");
			return;
		}
		disp_t t = c.type();
		uint16_t maxx = c.maxx();
		uint16_t maxy = c.maxy();
		if (LedCluster *l = LedCluster::getInstance()) {
			new SegmentDisplay(l,(SegmentDisplay::addrmode_t)t,maxx,maxy);
#ifdef CONFIG_PCF8574
		} else if (t == dt_pcf8574_hd44780u) {
			bool ok = false;
			if (PCF8574 *dev = PCF8574::getInstance()) {
				if (HD44780U *hd = new HD44780U(dev,maxx,maxy)) {
					hd->init();
					ok = true;
				}
			}
			if (!ok)
				log_warn(TAG,"no pcf8574/hd44780u found");
#endif
#ifdef CONFIG_SH1106
		} else if (t == dt_sh1106) {
			if (SH1106 *dev = SH1106::getInstance())
				dev->init(maxx,maxy,c.options());
			else
				log_warn(TAG,"no ssd1306 found");
#endif
#ifdef CONFIG_SSD1306
		} else if (t == dt_ssd1306) {
			if (SSD1306 *dev = SSD1306::getInstance())
				dev->init(maxx,maxy,c.options());
			else
				log_warn(TAG,"no ssd1306 found");
#endif
#ifdef CONFIG_SSD1309
		} else if (t == dt_ssd1309) {
			if (SSD1309 *dev = SSD1309::getInstance())
				dev->init(maxx,maxy,c.options());
			else
				log_warn(TAG,"no ssd1309 found");
#endif
#ifdef CONFIG_ILI9341
		} else if (t == dt_ili9341) {
			if (ILI9341 *dev = ILI9341::getInstance())
				dev->init(maxx,maxy,c.options());
			else
				log_warn(TAG,"no ili9341 found");
#endif
		} else {
			log_warn(TAG,"display configured, but none available");
			return;
		}
		TextDisplay *d = TextDisplay::getFirst();
		if (0 == d)
			return;
		MatrixDisplay *md = d->toMatrixDisplay();
		if (0 == md)
			return;
		md->init();	// to load the fonts
		const ScreenConfig &s = Config.screen();
		if (s.has_font_tiny()) {
			const char *fn = s.font_tiny().c_str();
			if (const Font *f = md->getFont(fn))
				md->setFont(font_tiny,f);
			else
				log_warn(TAG,"unknown tiny font '%s'",fn);
		}
		if (s.has_font_small()) {
			const char *fn = s.font_small().c_str();
			if (const Font *f = md->getFont(fn))
				md->setFont(font_small,f);
			else
				log_warn(TAG,"unknown small font '%s'",fn);
		}
		if (s.has_font_medium()) {
			const char *fn = s.font_medium().c_str();
			if (const Font *f = md->getFont(fn))
				md->setFont(font_medium,f);
			else
				log_warn(TAG,"unknown medium font '%s'",fn);
		}
		if (s.has_font_large()) {
			const char *fn = s.font_large().c_str();
			if (const Font *f = md->getFont(fn))
				md->setFont(font_large,f);
			else
				log_warn(TAG,"unknown large font '%s'",fn);
		}
	}
}

#endif
