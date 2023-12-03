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
	"<undef>\0action\0adc\0alarms\0apds\0bh1750\0bmx\0bq25\0button\0buzzer\0cam\0ccs811b\0cfg\0con\0cyclic\0dht\0dim\0disp\0ds18b20\0event\0fs\0ftpd\0gpio\0hcsr04\0hd44780u\0hdc1000\0hlw8012\0ht16k33\0http\0i2c\0ili9341\0ina219\0influx\0init\0led\0ledc\0log\0lua\0lwtcp\0max7219\0mcp230xx\0mqtt\0nightsky\0ns\0nvm\0opt3001\0ota\0owb\0pca9685\0pcf8574\0relay\0rgbleds\0romfs\0screen\0sgp30\0shell\0si7021\0sm\0sntp\0spi\0ssd130x\0sx1276\0tca9555\0telnet\0ti\0timefuse\0tlc5916\0tlc5947\0tp\0uart\0udns\0udpctrl\0usb\0wlan\0ws2812\0www\0xio\0xpt2046\0";

const uint16_t ModNameOff[] = {
	0,
	8,	// action
	15,	// adc
	19,	// alarms
	26,	// apds
	31,	// bh1750
	38,	// bmx
	42,	// bq25
	47,	// button
	54,	// buzzer
	61,	// cam
	65,	// ccs811b
	73,	// cfg
	77,	// con
	81,	// cyclic
	88,	// dht
	92,	// dim
	96,	// disp
	101,	// ds18b20
	109,	// event
	115,	// fs
	118,	// ftpd
	123,	// gpio
	128,	// hcsr04
	135,	// hd44780u
	144,	// hdc1000
	152,	// hlw8012
	160,	// ht16k33
	168,	// http
	173,	// i2c
	177,	// ili9341
	185,	// ina219
	192,	// influx
	199,	// init
	204,	// led
	208,	// ledc
	213,	// log
	217,	// lua
	221,	// lwtcp
	227,	// max7219
	235,	// mcp230xx
	244,	// mqtt
	249,	// nightsky
	258,	// ns
	261,	// nvm
	265,	// opt3001
	273,	// ota
	277,	// owb
	281,	// pca9685
	289,	// pcf8574
	297,	// relay
	303,	// rgbleds
	311,	// romfs
	317,	// screen
	324,	// sgp30
	330,	// shell
	336,	// si7021
	343,	// sm
	346,	// sntp
	351,	// spi
	355,	// ssd130x
	363,	// sx1276
	370,	// tca9555
	378,	// telnet
	385,	// ti
	388,	// timefuse
	397,	// tlc5916
	405,	// tlc5947
	413,	// tp
	416,	// uart
	421,	// udns
	426,	// udpctrl
	434,	// usb
	438,	// wlan
	443,	// ws2812
	450,	// www
	454,	// xio
	458,	// xpt2046
};
