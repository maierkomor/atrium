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
	"<undef>\0action\0adc\0ads1x\0alarms\0apds\0bh1750\0bmx\0bq25\0button\0buzzer\0cam\0ccs811b\0cfg\0con\0cyclic\0dht\0dim\0disp\0ds18b20\0event\0fs\0ftpd\0gpio\0hcsr04\0hd44780u\0hdc1000\0hlw8012\0ht16k33\0http\0i2c\0ili9341\0ina219\0influx\0init\0led\0ledc\0log\0lua\0lwtcp\0max7219\0mcp230xx\0mqtt\0nightsky\0ns\0nvm\0opt3001\0ota\0owb\0pca9685\0pcf8574\0relay\0rgbleds\0romfs\0screen\0sgp30\0shell\0si7021\0sm\0sntp\0spi\0ssd130x\0sx1276\0tca9555\0telnet\0ti\0timefuse\0tlc5916\0tlc5947\0tp\0uart\0udns\0udpctrl\0usb\0wlan\0ws2812\0www\0xio\0xpt2046\0";

const uint16_t ModNameOff[] = {
	0,
	8,	// action
	15,	// adc
	19,	// ads1x
	25,	// alarms
	32,	// apds
	37,	// bh1750
	44,	// bmx
	48,	// bq25
	53,	// button
	60,	// buzzer
	67,	// cam
	71,	// ccs811b
	79,	// cfg
	83,	// con
	87,	// cyclic
	94,	// dht
	98,	// dim
	102,	// disp
	107,	// ds18b20
	115,	// event
	121,	// fs
	124,	// ftpd
	129,	// gpio
	134,	// hcsr04
	141,	// hd44780u
	150,	// hdc1000
	158,	// hlw8012
	166,	// ht16k33
	174,	// http
	179,	// i2c
	183,	// ili9341
	191,	// ina219
	198,	// influx
	205,	// init
	210,	// led
	214,	// ledc
	219,	// log
	223,	// lua
	227,	// lwtcp
	233,	// max7219
	241,	// mcp230xx
	250,	// mqtt
	255,	// nightsky
	264,	// ns
	267,	// nvm
	271,	// opt3001
	279,	// ota
	283,	// owb
	287,	// pca9685
	295,	// pcf8574
	303,	// relay
	309,	// rgbleds
	317,	// romfs
	323,	// screen
	330,	// sgp30
	336,	// shell
	342,	// si7021
	349,	// sm
	352,	// sntp
	357,	// spi
	361,	// ssd130x
	369,	// sx1276
	376,	// tca9555
	384,	// telnet
	391,	// ti
	394,	// timefuse
	403,	// tlc5916
	411,	// tlc5947
	419,	// tp
	422,	// uart
	427,	// udns
	432,	// udpctrl
	440,	// usb
	444,	// wlan
	449,	// ws2812
	456,	// www
	460,	// xio
	464,	// xpt2046
};
