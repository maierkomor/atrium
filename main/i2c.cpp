/*
 *  Copyright (C) 2021-2022, Thomas Maier-Komor
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
#include "pcf8574.h"
#include "log.h"
#include "terminal.h"
#include "env.h"

#ifdef CONFIG_MCP2300X
#include "mcp2300x.h"
#endif
#ifdef CONFIG_MCP2301X
#include "mcp2301x.h"
#endif

#define TAG MODULE_I2C


#ifdef CONFIG_I2C_XDEV
static inline void i2c_scan_device(uint8_t bus, uint8_t addr, i2cdrv_t drv)
{
	switch (drv) {
#ifdef CONFIG_PCF8574
	case i2cdrv_pcf8574:
		PCF8574::create(bus,addr);
		break;
#endif
#ifdef CONFIG_MCP2300X
	case i2cdrv_mcp2300x:
		MCP2300X::create(bus,addr);
		break;
#endif
#ifdef CONFIG_MCP2301X
	case i2cdrv_mcp2301x:
		MCP2301X::create(bus,addr);
		break;
#endif
	default:
		log_warn(TAG,"request to look for unknown i2c device %d at address %u,0x%x",drv,bus,addr);
	}
}
#endif


int i2c_setup(void)
{
	for (const I2CConfig &c : HWConf.i2c()) {
		if (c.has_sda() && c.has_scl()) {
			uint8_t bus = c.port();
#ifdef CONFIG_IDF_TARGET_ESP8266
			int r = i2c_init(bus,c.sda(),c.scl(),0,c.xpullup());
#else
			int r = i2c_init(bus,c.sda(),c.scl(),c.freq(),c.xpullup());
#endif
			if (r < 0) 
				log_warn(TAG,"error %d",r);
#ifdef CONFIG_I2C_XDEV
			for (i2cdev_t d : c.devices()) {
				i2c_scan_device(bus,d & 0x7f,(i2cdrv_t)((d >> 8) & 0xff));
			}
#endif
		}
	}
	I2CDevice *d = I2CDevice::getFirst();
	while (d) {
		EnvObject *o = RTData->add(d->getName());
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
