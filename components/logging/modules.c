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
	"<undef>\0action\0adc\0alarms\0apds\0bh1750\0bmx\0button\0cam\0ccs811b\0cfg\0con\0cyclic\0dht\0dim\0disp\0ds18b20\0event\0fs\0ftpd\0gpio\0hcsr04\0hd44780u\0hdc1000\0hlw8012\0ht16k33\0http\0i2c\0ili9341\0ina219\0influx\0init\0led\0ledc\0log\0lua\0lwtcp\0max7219\0mcp230xx\0mqtt\0nightsky\0ns\0nvm\0ota\0owb\0pca9685\0pcf8574\0relay\0rgbleds\0romfs\0screen\0sgp30\0shell\0si7021\0sm\0sntp\0spi\0ssd130x\0sx1276\0tca9555\0telnet\0ti\0timefuse\0tlc5916\0tlc5947\0tp\0uart\0udns\0udpctrl\0usb\0wlan\0ws2812\0www\0xio\0xpt2046\0";

const uint16_t ModNameOff[] = {
	0,
	8,	// action
	15,	// adc
	19,	// alarms
	26,	// apds
	31,	// bh1750
	38,	// bmx
	42,	// button
	49,	// cam
	53,	// ccs811b
	61,	// cfg
	65,	// con
	69,	// cyclic
	76,	// dht
	80,	// dim
	84,	// disp
	89,	// ds18b20
	97,	// event
	103,	// fs
	106,	// ftpd
	111,	// gpio
	116,	// hcsr04
	123,	// hd44780u
	132,	// hdc1000
	140,	// hlw8012
	148,	// ht16k33
	156,	// http
	161,	// i2c
	165,	// ili9341
	173,	// ina219
	180,	// influx
	187,	// init
	192,	// led
	196,	// ledc
	201,	// log
	205,	// lua
	209,	// lwtcp
	215,	// max7219
	223,	// mcp230xx
	232,	// mqtt
	237,	// nightsky
	246,	// ns
	249,	// nvm
	253,	// ota
	257,	// owb
	261,	// pca9685
	269,	// pcf8574
	277,	// relay
	283,	// rgbleds
	291,	// romfs
	297,	// screen
	304,	// sgp30
	310,	// shell
	316,	// si7021
	323,	// sm
	326,	// sntp
	331,	// spi
	335,	// ssd130x
	343,	// sx1276
	350,	// tca9555
	358,	// telnet
	365,	// ti
	368,	// timefuse
	377,	// tlc5916
	385,	// tlc5947
	393,	// tp
	396,	// uart
	401,	// udns
	406,	// udpctrl
	414,	// usb
	418,	// wlan
	423,	// ws2812
	430,	// www
	434,	// xio
	438,	// xpt2046
};
