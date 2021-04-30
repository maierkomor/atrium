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

#include "actions.h"
#include "event.h"
#include "binformats.h"
#include "globals.h"
#include "i2cdrv.h"
#include "shell.h"
#include "terminal.h"
#include "ujson.h"

static const char TAG[] = "i2c";



int i2c_setup(void)
{
	unsigned num = 0;
	for (const I2CConfig &c : HWConf.i2c()) {
		if (c.has_sda() && c.has_scl()) {
#ifdef CONFIG_IDF_TARGET_ESP8266
			int r = i2c_init(c.port(),c.sda(),c.scl(),0);
#else
			int r = i2c_init(c.port(),c.sda(),c.scl(),c.freq());
#endif
			if (r > 0)
				num += r;
		}
	}
	bool o = RTData->get("temperature") || RTData->get("humidity");
	I2CSensor *d = I2CSensor::getFirst();
	if (num == 1)
		d->setName(d->drvName());
	while (d) {
		JsonObject *r = RTData;
		if (o || (num > 1)) {
			JsonObject *o = new JsonObject(d->getName());
			r->append(o);
			r = o;
		}
		d->attach(r);
		action_add(concat(d->getName(),"!sample"),I2CSensor::sample,d,d->getName());
		d = d->getNext();
	}
	return 0;
}


int i2c(Terminal &term, int argc, const char *args[])
{
	if (argc != 1)
		return arg_invnum(term);;
	I2CSensor *s = I2CSensor::getFirst();
	term.println("bus addr  name");
	while (s) {
		term.printf("%3d   %02x  %s",s->getBus(),s->getAddr(),s->getName());
		s = s->getNext();
	}
	return 0;
}
#endif
