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

#ifdef CONFIG_I2C

#include "globals.h"
#include "hwcfg.h"
#include "i2cdrv.h"
#include "log.h"
#include "terminal.h"
#include "ujson.h"

#define TAG MODULE_I2C


int i2c_setup(void)
{
	for (const I2CConfig &c : HWConf.i2c()) {
		if (c.has_sda() && c.has_scl()) {
#ifdef CONFIG_IDF_TARGET_ESP8266
			int r = i2c_init(c.port(),c.sda(),c.scl(),0,c.xpullup());
#else
			int r = i2c_init(c.port(),c.sda(),c.scl(),c.freq(),c.xpullup());
#endif
			if (r < 0) 
				log_warn(TAG,"error %d",r);
		}
	}
	I2CDevice *d = I2CDevice::getFirst();
	while (d) {
		JsonObject *o = RTData->add(d->getName());
		d->attach(o);
		d = d->getNext();
	}
	return 0;
}


int i2c(Terminal &term, int argc, const char *args[])
{
	if (argc != 1)
		return arg_invnum(term);;
	I2CDevice *s = I2CDevice::getFirst();
	term.println("bus addr  name");
	while (s) {
		term.printf("%3d   %02x  %s\n",s->getBus(),s->getAddr(),s->getName());
		s = s->getNext();
	}
	return 0;
}
#endif
