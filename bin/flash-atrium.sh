#!/bin/bash

#
#  Copyright (C) 2018-2023, Thomas Maier-Komor
#  Atrium Firmware Package for ESP
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#  

if [ "" == "$1" ]; then
	echo Pass the project configruation as an argument that you would like to flash.
	echo This script will then erase the attached ESP device and flash the Atrium firmware.
	exit
fi

if [ ! -d "$1" ]; then
	echo Project directory with flash images not found.
	exit
fi

echo This script flashes the Atrim firmware to an appropriate Espressif controller.
echo This will ERASE the whole flash of the device.
echo Hit enter, if you know what you are doing, and you want to continue.
echo Otherwise press CTRL-C to cancel.

read

which esptool.py > /dev/null || pip install esptool > /dev/null

ESPTOOL=`which esptool.py`
if [ ! -x "$ESPTOOL" ]; then
	echo esptool not found
	exit
fi

fs=`echo $1|sed 's/.*_/_/'`
chip=`echo $1|sed 's/_.*//;s/-//'`
size=`echo $1|sed 's/.*-//'`

case $1 in
esp8285 | esp8266_4m )
	esptool.py --chip esp8266 erase_flash
	esptool.py --baud 1000000 --chip esp8266 write_flash 0x0000 $1/boot@0x0000.bin 0x8000 $1/ptable@0x8000.bin 0x10000 $1/atrium-app1@0x10000.bin
;;
esp32_4m | esp32-s2_4m)
	esptool.py --chip $chip erase_flash
	esptool.py --baud 1000000 --chip $chip write_flash 0x1000 $1/boot@0x1000.bin 0x8000 $1/ptable@0x8000.bin 0x100000 $1/atrium.bin
;;
esp32-c3_4m | esp32-c6_4m | esp32-s3_4m )
	esptool.py --chip $chip erase_flash
	esptool.py --baud 1000000 --chip $chip write_flash 0x0000 $1/boot@0x0000.bin 0x8000 $1/ptable@0x8000.bin 0x100000 $1/atrium.bin
;;
esp32-s3_8m )
	esptool.py --chip $chip erase_flash
	esptool.py --baud 1000000 --chip $chip write_flash 0x0000 $1/boot@0x0000.bin 0x8000 $1/ptable@0x8000.bin 0x200000 $1/atrium.bin
;;
*)
        echo Unsupported chip/flash configuration.
;;
esac

