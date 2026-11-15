/*
 *  Copyright (C) 2021-2026, Thomas Maier-Komor
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
#include "husb238.h"
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
static inline void i2c_scan_bus(uint8_t bus, i2cdrv_t drv)
{
	// these scans only on request, as they may misdetect other device
	switch (drv) {
	case i2cdrv_sh1106:
		sh1106_scan(bus);
		break;
	case i2cdrv_ssd1306:
		ssd1306_scan(bus);
		break;
	default:
		break;
	}
}


static inline I2CDevice *i2c_scan_device(uint8_t bus, uint8_t addr, i2cdrv_t drv)
{
	// address in use checking must be performed in xx::create
	// addr is in 8bit format
	i2c_stop(bus);
	switch (drv) {
	case i2cdrv_invalid:
		break;
#ifdef CONFIG_PCF8574
	case i2cdrv_pcf8574:
		return PCF8574::create(bus,addr);
#endif
#ifdef CONFIG_MCP2300X
	case i2cdrv_mcp2300x:
		return MCP2300X::create(bus,addr);
#endif
#ifdef CONFIG_MCP2301X
	case i2cdrv_mcp2301x:
		return MCP2301X::create(bus,addr);
#endif
#ifdef CONFIG_HT16K33
	case i2cdrv_ht16k33:
		return HT16K33::create(bus,addr);
#endif
#ifdef CONFIG_PCA9685
	case i2cdrv_pca9685:
		return PCA9685::create(bus,addr,true,false);
	case i2cdrv_pca9685_npn:
		return PCA9685::create(bus,addr,false,true);
	case i2cdrv_pca9685_pnp:
		return PCA9685::create(bus,addr,true,true);
	case i2cdrv_pca9685_xclk:
		return PCA9685::create(bus,addr,true,false,true);
	case i2cdrv_pca9685_xclk_npn:
		return PCA9685::create(bus,addr,false,true,true);
	case i2cdrv_pca9685_xclk_pnp:
		return PCA9685::create(bus,addr,true,true,true);
#endif
#ifdef CONFIG_TCA9555
	case i2cdrv_tca9555:
		return TCA9555::create(bus,addr);
#endif
#ifdef CONFIG_INA2XX
	case i2cdrv_ina219:
		return INA2XX::create(bus,addr,ID_INA219);
	case i2cdrv_ina220:
		return INA2XX::create(bus,addr,ID_INA220);
#endif
#ifdef CONFIG_SI7021
	case i2cdrv_si7021:
		return SI7021::create(bus,addr);
#endif
#ifdef CONFIG_ADS1X1X
	case i2cdrv_ads1013:
		return ADS1x1x::create(bus,addr,ADS1x1x::ads1013);
	case i2cdrv_ads1014:
		return ADS1x1x::create(bus,addr,ADS1x1x::ads1014);
	case i2cdrv_ads1015:
		return ADS1x1x::create(bus,addr,ADS1x1x::ads1015);
	case i2cdrv_ads1113:
		return ADS1x1x::create(bus,addr,ADS1x1x::ads1113);
	case i2cdrv_ads1114:
		return ADS1x1x::create(bus,addr,ADS1x1x::ads1114);
	case i2cdrv_ads1115:
		return ADS1x1x::create(bus,addr,ADS1x1x::ads1115);
#endif
#ifdef CONFIG_SSD1306
	case i2cdrv_ssd1306:
		return SSD1306::create(bus,addr);
		break;
#endif
#ifdef CONFIG_SH1106
	case i2cdrv_sh1106:
		return SH1106::create(bus,addr);
#endif
#ifdef CONFIG_AHTXX
	case i2cdrv_aht10:
		return AHTXX::create(bus,AHTXX::aht10);
	case i2cdrv_aht20:
		return AHTXX::create(bus,AHTXX::aht20);
	case i2cdrv_aht30:
		return AHTXX::create(bus,AHTXX::aht30);
#endif
#ifdef CONFIG_HUSB238
	case i2cdrv_husb238:
		return HUSB238::create(bus,addr);
#endif
	default:
		log_warn(TAG,"unsupported I2C config %d at %u,0x%x",drv,bus,addr);
	}
	return 0;
}
#endif


#ifdef CONFIG_I2C_XDEV 
static void setup_device(uint8_t bus, i2cdev_t d)
{
	uint8_t addr = d & 0x7f;	// address in config is in 7bit format
	log_info(TAG,"config 0x%x for address 0x%x",d,addr);
	addr <<= 1;
	I2CDevice *dev = I2CDevice::getByAddr(bus,addr);
	if (0 == dev) {
		i2cdrv_t drv = (i2cdrv_t)((d >> 8) & 0xff);
		if (drv) {
			if (addr) {
				dev = i2c_scan_device(bus,addr,drv);
			} else {
				i2c_scan_bus(bus,drv);
				//dev = I2CDevice::getByDrv(bus,drv);
				// currently no interrupt set support
				// for devices without address
				return;
			}
		}
		if (0 == dev)
			return;
	}
	if (uint8_t intr = (d >> 16) & 0x3f) {
		--intr;
		log_info(TAG,"interrupt for %u,%x on GPIO %u",bus,addr>>1,intr);
		dev->addIntr(intr);
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
				log_warn(TAG,"i2c_init: %d",r);
#ifdef CONFIG_I2C_XDEV
			for (i2cdev_t d : c.devices()) {
				setup_device(bus,d);
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
	if (0 == strcmp(args[1],"reset")) {
		uint8_t bus = 0;
		if (argc > 2) {
			char *e;
			long l = strtol(args[2],&e,0);
			if ((l < 0) || (l > UINT8_MAX))
				return "Invalid bus id.";
			bus = l;
		}
		uint8_t cmd[] = { 0x00, 0x06 };	// general call reset (cannot be ack'ed)
		if (esp_err_t e = i2c_write_nostop(bus,cmd,sizeof(cmd)))
			return esp_err_to_name(e);
		return 0;
	}
	while (s) {
		if (0 == strcmp(s->getName(),args[1]))
			return s->exeCmd(term,argc-2,args+2);
		s = s->getNext();
	}
#endif
#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_1MB
	if (0 == strcmp("-w",args[1])) {
		if (argc < 5)
			return "Invalid number of arguments.";
		char *e;
		long l = strtol(args[2],&e,0);
		if ((l < 0) || (l > UINT8_MAX))
			return "Invalid bus id.";
		uint8_t bus = l;
		l = strtol(args[3],&e,0);
		if ((l < 0) || (l > 0x7f))
			return "Invalid address.";
		uint8_t addr = l<<1;
		uint8_t data[argc - 4];
		data[0] = addr<<1;
		for (int x = 4; x < argc; ++x) {
			long l = strtol(args[x],&e,0);
			if ((l < 0) || (l > UINT8_MAX))
				return "Invalid hex.";
			data[x-3] = l;
		}
		if (esp_err_t e = i2c_write(bus,data,sizeof(data)))
			term.printf("error: %s\n",esp_err_to_name(e));
	} else if (0 == strcmp("-r",args[1])) {
		if (argc < 5)
			return "Invalid number of arguments.";
		char *e;
		long l = strtol(args[2],&e,0);
		if ((l < 0) || (l > UINT8_MAX))
			return "Invalid bus id.";
		uint8_t bus = l;
		l = strtol(args[3],&e,0);
		if ((l < 0) || (l > 0x7f))
			return "Invalid address.";
		uint8_t addr = l<<1;
		l = strtol(args[4],&e,0);
		if ((l < 0) || (l > 0x10))
			return "Invalid size.";
		uint8_t din[l];
		memset(din,0,sizeof(din));
		if (argc == 6) {
			l = strtol(args[5],&e,0);
			if ((l < 0) || (l > UINT8_MAX))
				return "Invalid register.";
			uint8_t reg = l;
			if (esp_err_t e = i2c_w1rd(bus,addr,reg,din,sizeof(din)))
				return esp_err_to_name(e);
			else
				term.print_hex(din,sizeof(din));

		} else {
			if (esp_err_t e = i2c_read(bus,addr,din,sizeof(din)))
				return esp_err_to_name(e);
			else
				term.print_hex(din,sizeof(din));
		}
	} else {
		return "Invalid argument #1.";;
	}
#endif
	return 0;
}
#endif
