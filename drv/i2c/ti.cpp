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

#include "hdc1000.h"
#include "log.h"

#define DEV_ADDR	(0x40<<1)
#define REG_MANU_ID	0xfe	// manufacturer id register
#define REG_DEV_ID	0xff	// device id register for TI
#define DEV_ADDR_LOW	(0x44<<1)
#define DEV_ADDR_HIGH	(0x47<<1)

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
	log_dbug(TAG,"found TI device 0x%04x",id);
#ifdef CONFIG_HDC1000
	if ((id = 0x1000) || (id == 0x1050)) {
		if (I2CDevice *s = HDC1000::create(bus,addr,id)) {
			s->init();
			return 1;
		}
	}
#endif
#ifdef CONFIG_OPT3001
	if (id == 0x3001) {
		if (I2CDevice *s = OPT3001::create(bus,addr)) {
			s->init();
			return 1;
		}
	}
#endif
	return 0;

}


unsigned ti_scan(uint8_t bus)
{
	unsigned n = 0;
	log_info(TAG,"search TI devices on bus %u",bus);
	scan_addr(bus,DEV_ADDR);
	for (uint8_t addr = DEV_ADDR_LOW; addr <= DEV_ADDR_HIGH; addr += 2)
		scan_addr(bus,addr);
	return n;
}

#endif
