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
	"<undef>\0action\0adc\0ads1x\0aht\0alarms\0apds\0bh1750\0bmx\0bq25\0button\0buzzer\0cam\0ccs811b\0cfg\0con\0cyclic\0dht\0dim\0disp\0ds18b20\0event\0fs\0ftpd\0gpio\0hcsr04\0hd44780u\0hdc1000\0hlw8012\0ht16k33\0http\0husb238\0i2c\0ili9341\0ina2xx\0influx\0init\0led\0ledc\0log\0lua\0lwtcp\0max7219\0mcp230xx\0mqtt\0nightsky\0ns\0nvm\0opt3001\0ota\0owb\0pca9685\0pcf8574\0relay\0rgbleds\0romfs\0screen\0sgp\0shell\0sht4x\0si7021\0sm\0sntp\0spi\0ssd130x\0sx1276\0tca9555\0telnet\0ti\0timefuse\0tlc5916\0tlc5947\0tp\0uart\0udns\0udpctrl\0usb\0wlan\0ws2812\0www\0xio\0xplane\0xpt2046\0";

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
	183,	// husb238
	191,	// i2c
	195,	// ili9341
	203,	// ina2xx
	210,	// influx
	217,	// init
	222,	// led
	226,	// ledc
	231,	// log
	235,	// lua
	239,	// lwtcp
	245,	// max7219
	253,	// mcp230xx
	262,	// mqtt
	267,	// nightsky
	276,	// ns
	279,	// nvm
	283,	// opt3001
	291,	// ota
	295,	// owb
	299,	// pca9685
	307,	// pcf8574
	315,	// relay
	321,	// rgbleds
	329,	// romfs
	335,	// screen
	342,	// sgp
	346,	// shell
	352,	// sht4x
	358,	// si7021
	365,	// sm
	368,	// sntp
	373,	// spi
	377,	// ssd130x
	385,	// sx1276
	392,	// tca9555
	400,	// telnet
	407,	// ti
	410,	// timefuse
	419,	// tlc5916
	427,	// tlc5947
	435,	// tp
	438,	// uart
	443,	// udns
	448,	// udpctrl
	456,	// usb
	460,	// wlan
	465,	// ws2812
	472,	// www
	476,	// xio
	480,	// xplane
	487,	// xpt2046
};
