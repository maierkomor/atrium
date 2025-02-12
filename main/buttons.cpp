/*
 *  Copyright (C) 2018-2025, Thomas Maier-Komor
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

#ifdef CONFIG_BUTTON

#include "actions.h"
#include "button.h"
#include "event.h"
#include "globals.h"
#include "hwcfg.h"
#include "log.h"
#include "settings.h"
#ifdef CONFIG_ROTARYENCODER
#include "rotenc.h"
#endif

#include <driver/gpio.h>

#include <string.h>

using namespace std;

#define TAG MODULE_BUTTON


void button_setup()
{
	for (auto &c : *HWConf.mutable_button()) {
		if (!c.has_name())
			continue;
		int8_t gpio = c.gpio();
#ifndef CONFIG_ROTARYENCODER
		if (gpio < 0)
			continue;
#endif
		const char *n = c.name().c_str();
		if (*n == 0) {
			char name[12];
			sprintf(name,"button@%u",gpio);
			c.set_name(name);
			n = c.name().c_str();
		}
		if (Button::find(n)) {
			log_warn(TAG,"button %s already exists: ignoring additional config",n);
			continue;
		}
		bool al = c.presslvl();
		xio_cfg_pull_t pullmode = xio_cfg_pull_none;
		if (c.pull_mode() == pull_up) 
			pullmode = xio_cfg_pull_up;
		else if (c.pull_mode() == pull_down) 
			pullmode = xio_cfg_pull_down;
#ifdef CONFIG_ROTARYENCODER
		int8_t clk = c.clk();
		int8_t dt = c.dt();
		if ((dt != -1) && (clk != -1)) {
			RotaryEncoder::create(n,(xio_t)clk,(xio_t)dt,gpio == -1 ? XIO_INVALID : (xio_t)gpio);
		} else
#endif
		if (Button *b = Button::create(n,(xio_t)gpio,pullmode,al)) {
			b->attach(RTData);
		}
	}
}

#endif
