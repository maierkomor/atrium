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

#include "modules.h"

const char ModNames[] =
	"<undef>\0action\0adc\0ads1x\0aht\0alarms\0apds\0bh1750\0bmx\0bq25\0button\0buzzer\0cam\0ccs811b\0cfg\0con\0cyclic\0dht\0dim\0disp\0ds18b20\0event\0fs\0ftpd\0gpio\0hcsr04\0hd44780u\0hdc1000\0hlw8012\0ht16k33\0http\0i2c\0ili9341\0ina2xx\0influx\0init\0led\0ledc\0log\0lua\0lwtcp\0max7219\0mcp230xx\0mqtt\0nightsky\0ns\0nvm\0opt3001\0ota\0owb\0pca9685\0pcf8574\0relay\0rgbleds\0romfs\0screen\0sgp30\0shell\0si7021\0sm\0sntp\0spi\0ssd130x\0sx1276\0tca9555\0telnet\0ti\0timefuse\0tlc5916\0tlc5947\0tp\0uart\0udns\0udpctrl\0usb\0wlan\0ws2812\0www\0xio\0xplane\0xpt2046\0";

const uint16_t ModNameOff[] = {
	0,
	8,	// action
	15,	// adc
	19,	// ads1x
	25,	// aht
	29,	// alarms
	36,	// apds
	41,	// bh1750
	48,	// bmx
	52,	// bq25
	57,	// button
	64,	// buzzer
	71,	// cam
	75,	// ccs811b
	83,	// cfg
	87,	// con
	91,	// cyclic
	98,	// dht
	102,	// dim
	106,	// disp
	111,	// ds18b20
	119,	// event
	125,	// fs
	128,	// ftpd
	133,	// gpio
	138,	// hcsr04
	145,	// hd44780u
	154,	// hdc1000
	162,	// hlw8012
	170,	// ht16k33
	178,	// http
	183,	// i2c
	187,	// ili9341
	195,	// ina2xx
	202,	// influx
	209,	// init
	214,	// led
	218,	// ledc
	223,	// log
	227,	// lua
	231,	// lwtcp
	237,	// max7219
	245,	// mcp230xx
	254,	// mqtt
	259,	// nightsky
	268,	// ns
	271,	// nvm
	275,	// opt3001
	283,	// ota
	287,	// owb
	291,	// pca9685
	299,	// pcf8574
	307,	// relay
	313,	// rgbleds
	321,	// romfs
	327,	// screen
	334,	// sgp30
	340,	// shell
	346,	// si7021
	353,	// sm
	356,	// sntp
	361,	// spi
	365,	// ssd130x
	373,	// sx1276
	380,	// tca9555
	388,	// telnet
	395,	// ti
	398,	// timefuse
	407,	// tlc5916
	415,	// tlc5947
	423,	// tp
	426,	// uart
	431,	// udns
	436,	// udpctrl
	444,	// usb
	448,	// wlan
	453,	// ws2812
	460,	// www
	464,	// xio
	468,	// xplane
	475,	// xpt2046
};
