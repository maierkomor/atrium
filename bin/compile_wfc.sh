#!/bin/bash

#
#  Copyright (C) 2018-2020, Thomas Maier-Komor
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

if [ "$PROJECT" == "" ]; then
	echo error: target not set
	exit 1
fi

if [ ! -x "$WFC" ]; then
	echo "unable to find wfc"
	exit 1
fi

TARGET=`grep CONFIG_WFC_TARGET "projects/$PROJECT" | sed 's/.*="//;s/"//'`

if [ "$TARGET" == "" ]; then
	TARGET=$CHIP
fi
echo generating wfc for target $TARGET, project $PROJECT

WFC=${WFC:=$WFC_DIR/bin/wfc}
v=`$WFC -V | head -1 | sed 's/.*Version //'`
if [[ "$v" < "R2003" ]]; then
	echo WFC is too old. Please update WFC.
	exit 1
fi

$WFC -t $TARGET binformats.wfc
if [ "$?" != "0" ]; then
	echo WFC compilation failed
	exit 1
fi

update_h=0
if [ -f main/binformats.h ]; then
	diffsize=`diff binformats.h main/binformats.h|wc -l`
	if [ "$diffsize" -gt "4" ]; then
		#echo binformats.h is out-of-date
		update_h=1
	fi
else
	update_h=1
fi

update_cpp=0
if [ -f main/binformats.cpp ]; then
	diffsize=`diff binformats.cpp main/binformats.cpp|wc -l`
	if [ "$diffsize" -gt "4" ]; then
		#echo binformats.cpp is out-of-date
		update_cpp=1
	fi
else
	update_cpp=1
fi

if [ "$update_h" == "1" ]; then
	echo updating main/binformats.h
	mv binformats.h main
else
	echo binformats.h is up-to-date
	rm binformats.h
fi

if [ "$update_cpp" == "1" ]; then
	echo updating main/binformats.cpp
	mv binformats.cpp main
else
	echo binformats.cpp is up-to-date
	rm binformats.cpp
fi
exit 0
