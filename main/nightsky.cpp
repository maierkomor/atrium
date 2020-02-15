/*
 *  Copyright (C) 2018-2019, Thomas Maier-Komor
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

#ifdef CONFIG_NIGHTSKY

#include "actions.h"
#include "cyclic.h"
#include "globals.h"
#include "log.h"
#include "nightsky.h"
#include "tlc5947.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


static char TAG[] = "nightsky";

static TLC5947 *Drv = 0;


static void nightsky_init()
{
	Drv = new TLC5947( (gpio_num_t)CONFIG_NIGHTSKY_IO_SIN
			, (gpio_num_t)CONFIG_NIGHTSKY_IO_SCLK
			, (gpio_num_t)CONFIG_NIGHTSKY_IO_XLAT
			, (gpio_num_t)CONFIG_NIGHTSKY_IO_BLANK
			, CONFIG_NIGHTSKY_NTLC);
	Drv->init();
	Drv->off();
	for (int i = 0; i < CONFIG_NIGHTSKY_NTLC*24; ++i)
		Drv->set_led(i,40*i);
	Drv->commit();
	Drv->on();
}


static unsigned nightsky_step()
{
	static uint16_t v = 0;
	if (v >= (1<<12))
		v = 0;
	for (int i = 0; i < CONFIG_NIGHTSKY_NTLC*24; ++i)
		Drv->set_led(i,v);
	v += 0x80;
	Drv->commit();
	return 100;
}


extern "C"
void nightsky_setup()
{
	nightsky_init();
	add_cyclic_task("nightsky",nightsky_step);
	log_info(TAG,"started");
}

#endif
