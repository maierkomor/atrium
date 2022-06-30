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

#include "modules.h"

const char ModNames[] =
	"<undef>\0action\0adc\0alarms\0apds\0bh1750\0bmx\0button\0cam\0ccs811b\0cfg\0con\0cyclic\0dht\0dim\0disp\0ds18b20\0event\0fs\0ftpd\0func\0gpio\0hcsr04\0hd44780u\0hdc1000\0ht16k33\0http\0i2c\0inetd\0influx\0init\0led\0ledc\0ledstrip\0log\0lwtcp\0max7219\0mcp230xx\0mqtt\0nightsky\0ns\0ota\0owb\0pca9685\0pcf8574\0relay\0romfs\0screen\0sgp30\0shell\0signal\0sm\0sntp\0ssd1306\0telnet\0ti\0timefuse\0tlc5916\0tlc5947\0tp\0uart\0udns\0udpctrl\0wlan\0ws2812\0www\0xio\0";

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
	111,	// func
	116,	// gpio
	121,	// hcsr04
	128,	// hd44780u
	137,	// hdc1000
	145,	// ht16k33
	153,	// http
	158,	// i2c
	162,	// inetd
	168,	// influx
	175,	// init
	180,	// led
	184,	// ledc
	189,	// ledstrip
	198,	// log
	202,	// lwtcp
	208,	// max7219
	216,	// mcp230xx
	225,	// mqtt
	230,	// nightsky
	239,	// ns
	242,	// ota
	246,	// owb
	250,	// pca9685
	258,	// pcf8574
	266,	// relay
	272,	// romfs
	278,	// screen
	285,	// sgp30
	291,	// shell
	297,	// signal
	304,	// sm
	307,	// sntp
	312,	// ssd1306
	320,	// telnet
	327,	// ti
	330,	// timefuse
	339,	// tlc5916
	347,	// tlc5947
	355,	// tp
	358,	// uart
	363,	// udns
	368,	// udpctrl
	376,	// wlan
	381,	// ws2812
	388,	// www
	392,	// xio
};
