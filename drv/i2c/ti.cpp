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

#include "hdc1000.h"
#include "log.h"

#define DEV_ADDR	(0x40<<1)
#define REG_MANU_ID	0xfe	// manufacturer id register
#define REG_DEV_ID	0xff	// device id register for TI

static const char TAG[] = "ti";

unsigned ti_scan(uint8_t bus)
{
	unsigned n = 0;
	uint8_t data[2];
	if (i2c_w1rd(bus,DEV_ADDR,REG_MANU_ID,data,sizeof(data)))
		return n;
	if ((data[0] != 0x54) || (data[1] != 0x49))
		return n;
	if (i2c_w1rd(bus,DEV_ADDR,REG_DEV_ID,data,sizeof(data)))
		return n;
	log_dbug(TAG,"found TI device %02x%02x",data[0],data[1]);
#ifdef CONFIG_HDC1000
	if ((data[0] == 0x10) && (data[1] == 0x50)) {
		if (I2CDevice *s = HDC1000::create(bus)) {
			s->init();
			++n;
		}
	}
#endif
	return n;
}

#endif
