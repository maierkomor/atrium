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
	"<undef>\0action\0adc\0alarms\0apds\0bh1750\0bmx\0button\0cam\0ccs811b\0cfg\0clock\0con\0cyclic\0dht\0dim\0disp\0ds18b20\0event\0fs\0ftpd\0func\0gpio\0hcsr04\0hd44780u\0hdc1000\0ht16k33\0http\0i2c\0inetd\0influx\0init\0ledc\0ledstrip\0log\0lwtcp\0max7219\0mcp230xx\0mqtt\0nightsky\0ns\0ota\0owb\0pcf8574\0relay\0romfs\0sgp30\0shell\0signal\0sntp\0ssd1306\0status\0telnet\0ti\0timefuse\0tlc5916\0tlc5947\0tp\0uart\0udns\0udpctrl\0wlan\0ws2812\0www\0xio\0";

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
	65,	// clock
	71,	// con
	75,	// cyclic
	82,	// dht
	86,	// dim
	90,	// disp
	95,	// ds18b20
	103,	// event
	109,	// fs
	112,	// ftpd
	117,	// func
	122,	// gpio
	127,	// hcsr04
	134,	// hd44780u
	143,	// hdc1000
	151,	// ht16k33
	159,	// http
	164,	// i2c
	168,	// inetd
	174,	// influx
	181,	// init
	186,	// ledc
	191,	// ledstrip
	200,	// log
	204,	// lwtcp
	210,	// max7219
	218,	// mcp230xx
	227,	// mqtt
	232,	// nightsky
	241,	// ns
	244,	// ota
	248,	// owb
	252,	// pcf8574
	260,	// relay
	266,	// romfs
	272,	// sgp30
	278,	// shell
	284,	// signal
	291,	// sntp
	296,	// ssd1306
	304,	// status
	311,	// telnet
	318,	// ti
	321,	// timefuse
	330,	// tlc5916
	338,	// tlc5947
	346,	// tp
	349,	// uart
	354,	// udns
	359,	// udpctrl
	367,	// wlan
	372,	// ws2812
	379,	// www
	383,	// xio
};
