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

#ifdef CONFIG_I2C

#include "globals.h"
#include "hwcfg.h"
#include "ht16k33.h"
#include "i2cdrv.h"
#include "ina2xx.h"
#include "pca9685.h"
#include "pcf8574.h"
#include "si7021.h"
#include "ssd1306.h"
#include "tca9555.h"
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
#ifdef CONFIG_HT16K33
	case i2cdrv_ht16k33:
		HT16K33::create(bus,addr);
		break;
#endif
#ifdef CONFIG_PCA9685
	case i2cdrv_pca9685:
		PCA9685::create(bus,addr,true,false);
		break;
	case i2cdrv_pca9685_npn:
		PCA9685::create(bus,addr,false,true);
		break;
	case i2cdrv_pca9685_pnp:
		PCA9685::create(bus,addr,true,true);
		break;
	case i2cdrv_pca9685_xclk:
		PCA9685::create(bus,addr,true,false,true);
		break;
	case i2cdrv_pca9685_xclk_npn:
		PCA9685::create(bus,addr,false,true,true);
		break;
	case i2cdrv_pca9685_xclk_pnp:
		PCA9685::create(bus,addr,true,true,true);
		break;
#endif
#ifdef CONFIG_TCA9555
	case i2cdrv_tca9555:
		TCA9555::create(bus,addr);
		break;
#endif
#ifdef CONFIG_INA2XX
	case i2cdrv_ina219:
		INA219::create(bus,addr);
		break;
#endif
#ifdef CONFIG_SI7021
	case i2cdrv_si7021:
		SI7021::create(bus,addr);
		break;
#endif
#ifdef CONFIG_SSD1306
	case i2cdrv_ssd1306:
		SSD1306::create(bus,addr);
		break;
#endif
	default:
		log_warn(TAG,"unsupported I2C config %d at %u,0x%x",drv,bus,addr);
	}
}
#endif


int i2c_setup(void)
{
	for (const I2CConfig &c : HWConf.i2c()) {
		if (c.has_sda() && c.has_scl()) {
			uint8_t bus = c.port();
			log_info(TAG,"bus%d: sda=%d, scl=%d",bus,c.sda(),c.scl());
#ifdef CONFIG_IDF_TARGET_ESP8266
			int r = i2c_init(bus,c.sda(),c.scl(),0,c.xpullup());
#else
			int r = i2c_init(bus,c.sda(),c.scl(),c.freq(),c.xpullup());
#endif
			if (r < 0) 
				log_warn(TAG,"error %d",r);
#ifdef CONFIG_I2C_XDEV 
			for (i2cdev_t d : c.devices()) {
				i2c_scan_device(bus,d & 0xff,(i2cdrv_t)((d >> 8) & 0xff));
			}
#endif
		}
	}
	I2CDevice *d = I2CDevice::getFirst();
	while (d) {
		EnvObject *o = new EnvObject(d->getName());
		d->attach(o);
		if (o->numChildren())
			RTData->add(o);
		else
			delete o;
		d = d->getNext();
	}
	return 0;
}


const char *i2c(Terminal &term, int argc, const char *args[])
{
	I2CDevice *s = I2CDevice::getFirst();
	if (argc == 1) {
		term.println("bus addr  name");
		while (s) {
			term.printf("%3d   %02x  %s\n",s->getBus(),s->getAddr(),s->getName());
			s = s->getNext();
		}
		return 0;
	}
#ifdef CONFIG_I2C_XCMD
	while (s) {
		if (0 == strcmp(s->getName(),args[1]))
			return s->exeCmd(term,argc-2,args+2);
		s = s->getNext();
	}
#endif
	return "Invalid argument #1.";;
}
#endif
