/*
 *  Copyright (C) 2021-2024, Thomas Maier-Komor
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
#include "ads1x1x.h"
#include "ahtxx.h"
#include "hwcfg.h"
#include "ht16k33.h"
#include "i2cdrv.h"
#include "ina2xx.h"
#include "pca9685.h"
#include "pcf8574.h"
#include "si7021.h"
#include "ssd1306.h"
#include "sh1106.h"
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
	// addr is in 8bit format
	switch (drv) {
	case i2cdrv_invalid:
		break;
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
		INA2XX::create(bus,addr,ID_INA219);
		break;
	case i2cdrv_ina220:
		INA2XX::create(bus,addr,ID_INA220);
		break;
#endif
#ifdef CONFIG_SI7021
	case i2cdrv_si7021:
		SI7021::create(bus,addr);
		break;
#endif
#ifdef CONFIG_ADS1X1X
	case i2cdrv_ads1013:
		ADS1x1x::create(bus,addr,ADS1x1x::ads1013);
		break;
	case i2cdrv_ads1014:
		ADS1x1x::create(bus,addr,ADS1x1x::ads1014);
		break;
	case i2cdrv_ads1015:
		ADS1x1x::create(bus,addr,ADS1x1x::ads1015);
		break;
	case i2cdrv_ads1113:
		ADS1x1x::create(bus,addr,ADS1x1x::ads1113);
		break;
	case i2cdrv_ads1114:
		ADS1x1x::create(bus,addr,ADS1x1x::ads1114);
		break;
	case i2cdrv_ads1115:
		ADS1x1x::create(bus,addr,ADS1x1x::ads1115);
		break;
#endif
#ifdef CONFIG_SSD1306
	case i2cdrv_ssd1306:
		if (addr)
			SSD1306::create(bus,addr);
		else
			ssd1306_scan(bus);
		break;
#endif
#ifdef CONFIG_SH1106
	case i2cdrv_sh1106:
		if (addr)
			SH1106::create(bus,addr);
		else
			sh1106_scan(bus);
		break;
#endif
#ifdef CONFIG_AHTXX
	case i2cdrv_aht10:
		aht_scan(bus,AHTXX::aht10);
		break;
	case i2cdrv_aht20:
		aht_scan(bus,AHTXX::aht20);
		break;
	case i2cdrv_aht30:
		aht_scan(bus,AHTXX::aht30);
		break;
#endif
	default:
		log_warn(TAG,"unsupported I2C config %d at %u,0x%x",drv,bus,addr);
	}
}
#endif


void i2c_setup(void)
{
	for (const I2CConfig &c : HWConf.i2c()) {
		if (c.has_sda() && c.has_scl()) {
			uint8_t bus = c.port();
			bool xpullup = c.xpullup();
			log_info(TAG,"bus%d: sda=%d, scl=%d, %sternal pull-up",bus,c.sda(),c.scl(),xpullup?"ex":"in");
			// i2c_init performs a bus scan of known devices
#ifdef CONFIG_IDF_TARGET_ESP8266
			int r = i2c_init(bus,c.sda(),c.scl(),0,xpullup);
#else
			int r = i2c_init(bus,c.sda(),c.scl(),c.freq(),xpullup);
#endif
			if (r < 0) 
				log_warn(TAG,"error %d",r);
#ifdef CONFIG_I2C_XDEV 
			for (i2cdev_t d : c.devices()) {
				uint8_t addr = d & 0x7f;	// address in config is in 7bit format
				log_info(TAG,"config 0x%x for address 0x%x",d,addr);
				addr <<= 1;
				I2CDevice *dev = I2CDevice::getByAddr(addr);
				if (0 == dev) {
					if (i2cdrv_t drv = (i2cdrv_t)((d >> 8) & 0xff)) {
						i2c_scan_device(bus,addr,drv);
						dev = I2CDevice::getByAddr(addr);
					}
				}
				uint8_t intr = (d >> 16) & 0x3f;
				if (dev && intr) {
					--intr;
					log_info(TAG,"interrupt for %u,%x on GPIO %u",bus,addr,intr);
					dev->addIntr(intr);
				} else {
					log_warn(TAG,"device %u,%x not found",bus,addr,intr);
				}
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
	char *e;
	long l = strtol(args[0],&e,0);
	if (*e == 0) {
		if ((l < 0) || (l > UINT8_MAX))
			return "Invalid bus id.";
		uint8_t bus = l;
		if (0 == strcmp(args[1],"reset")) {
			uint8_t cmd[] = { 0x00, 0x06 };	// get serial ID
			if (esp_err_t e = i2c_write(bus,cmd,sizeof(cmd),false,true))
				term.printf("error: %s\n",esp_err_to_name(e));
			return 0;
		} else {
			return "Invalid bus command.";
		}
	}
	while (s) {
		if (0 == strcmp(s->getName(),args[1]))
			return s->exeCmd(term,argc-2,args+2);
		s = s->getNext();
	}
#endif
	return "Invalid argument #1.";;
}
#endif
