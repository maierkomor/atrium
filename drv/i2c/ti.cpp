/*
 *  Copyright (C) 2021-2025, Thomas Maier-Komor
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

#include "hdc1000.h"
#include "ina2xx.h"
#include "opt3001.h"
#include "log.h"

#define REG_MANU_ID	0xfe	// manufacturer id register
#define REG_DEV_ID	0xff	// device id register for TI
#define DEV_ADDR_LOW	(0x40<<1)
#define DEV_ADDR_HIGH	(0x4f<<1)

#define TAG MODULE_TI


static unsigned scan_addr(uint8_t bus, uint8_t addr)
{
	uint8_t data[2];
	if (i2c_w1rd(bus,addr,REG_MANU_ID,data,sizeof(data)))
		return 0;
	if ((data[0] != 0x54) || (data[1] != 0x49))
		return 0;
	if (i2c_w1rd(bus,addr,REG_DEV_ID,data,sizeof(data)))
		return 0;
	uint16_t id = (data[0] << 8) | data[1];
	log_info(TAG,"found TI device 0x%04x",id);
#ifdef CONFIG_HDC1000
	if ((id == 0x1000) || (id == 0x1050)) {
		if (HDC1000::create(bus,addr,id)) {
			return 1;
		}
	}
#endif
#ifdef CONFIG_OPT3001
	if (id == 0x3001) {
		if (OPT3001::create(bus,addr)) {
			return 1;
		}
	}
#endif
#ifdef CONFIG_INA2XX
	if (id == 0x2260) {
		if (INA2XX::create(bus,addr,ID_INA226)) {
			return 1;
		}
	}
	if (id == 0x2270) {
		if (INA2XX::create(bus,addr,ID_INA260)) {
			return 1;
		}
	}
	if (id == 0xa080) {
		if (INA2XX::create(bus,addr,ID_INA236)) {
			return 1;
		}
	}
#endif
	log_warn(TAG,"no driver for device id 0x%04x",id);
	return 0;

}


unsigned ti_scan(uint8_t bus)
{
	unsigned n = 0;
	log_info(TAG,"search TI devices on bus %u",bus);
	for (uint8_t addr = DEV_ADDR_LOW; addr <= DEV_ADDR_HIGH; addr += 2)
		scan_addr(bus,addr);
	return n;
}

#endif
