
#!/bin/sh

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
	echo This script will then update the primary application image of the attached ESP device.
	exit
fi

if [ ! -d $1 ]; then
	echo Project directory with flash images not found.
	exit
fi

which esptool.py > /dev/null || pip install esptool > /dev/null

which esptool.py || exit

fs=`echo $1|sed 's/.*_/_/'`
chip=`echo $1|sed 's/_.*//;s/-//'`
size=`echo $1|sed 's/.*-//'`

case $chip in
esp8285 | esp8266_4m )
	esptool.py --baud 1000000 --chip esp8266 write_flash 0x10000 $1/atrium-app1@0x10000.bin
;;
esp32_4m | esp32-c3_4m | esp32-s2_4m | esp32-s3_4m )
	esptool.py --baud 1000000 --chip $chip write_flash 0x100000 $1/atrium.bin
;;
esp32-s3_8m )
	esptool.py --baud 1000000 --chip $chip write_flash 0x200000 $1/atrium.bin
;;
*)
        echo Unsupported chip/flash configuration.
;;
esac

