/*
 *  Copyright (C) 2018-2021, Thomas Maier-Komor
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

#include <driver/gpio.h>

#include <string.h>

using namespace std;

#if CONFIG_BUTTON_GPIO < 0 || CONFIG_BUTTON_GPIO >= GPIO_PIN_COUNT
#error gpio value for button out of range
#endif

#define TAG MODULE_BUTTON

static Button *Buttons = 0;


int button_setup()
{
	if (Buttons) {
		log_warn(TAG,"already initialized");
		return 1;
	}
	for (auto &c : *HWConf.mutable_button()) {
		if (!c.has_name() || !c.has_gpio())
			continue;
		int8_t gpio = c.gpio();
		if (gpio < 0)
			continue;
		const char *n = c.name().c_str();
		if (*n == 0) {
			char name[12];
			sprintf(name,"button@%u",gpio);
			c.set_name(name);
			n = c.name().c_str();
		}
		bool al = c.presslvl();
		gpio_pull_mode_t pullmode = GPIO_FLOATING;
		if (c.pull_mode() == pull_up) 
			pullmode = GPIO_PULLUP_ONLY;
		else if (c.pull_mode() == pull_down) 
			pullmode = GPIO_PULLDOWN_ONLY;
		Buttons = new Button(n,(gpio_num_t)gpio,pullmode,al,Buttons);
	}
	return 0;
}

#endif
