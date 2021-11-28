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
	"<undef>\0action\0adc\0alarms\0apds\0bmx\0button\0cam\0ccs811b\0cfg\0clock\0con\0cyclic\0dht\0dim\0disp\0ds18b20\0event\0fs\0ftpd\0func\0gpio\0hcsr04\0hd44780u\0hdc1000\0ht16k33\0http\0i2c\0inetd\0influx\0init\0ledc\0ledstrip\0log\0lwtcp\0max7219\0mcp25017\0mqtt\0nightsky\0ns\0ota\0owb\0pcf8574\0relay\0romfs\0sgp30\0shell\0signal\0sntp\0ssd1306\0status\0telnet\0ti\0timefuse\0tlc5916\0tlc5947\0tp\0uart\0udns\0udpctrl\0wlan\0ws2812\0www\0";

const uint16_t ModNameOff[] = {
	0,
	8,	// action
	15,	// adc
	19,	// alarms
	26,	// apds
	31,	// bmx
	35,	// button
	42,	// cam
	46,	// ccs811b
	54,	// cfg
	58,	// clock
	64,	// con
	68,	// cyclic
	75,	// dht
	79,	// dim
	83,	// disp
	88,	// ds18b20
	96,	// event
	102,	// fs
	105,	// ftpd
	110,	// func
	115,	// gpio
	120,	// hcsr04
	127,	// hd44780u
	136,	// hdc1000
	144,	// ht16k33
	152,	// http
	157,	// i2c
	161,	// inetd
	167,	// influx
	174,	// init
	179,	// ledc
	184,	// ledstrip
	193,	// log
	197,	// lwtcp
	203,	// max7219
	211,	// mcp25017
	220,	// mqtt
	225,	// nightsky
	234,	// ns
	237,	// ota
	241,	// owb
	245,	// pcf8574
	253,	// relay
	259,	// romfs
	265,	// sgp30
	271,	// shell
	277,	// signal
	284,	// sntp
	289,	// ssd1306
	297,	// status
	304,	// telnet
	311,	// ti
	314,	// timefuse
	323,	// tlc5916
	331,	// tlc5947
	339,	// tp
	342,	// uart
	347,	// udns
	352,	// udpctrl
	360,	// wlan
	365,	// ws2812
	372,	// www
};
