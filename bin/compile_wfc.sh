#!/bin/bash

#
#  Copyright (C) 2018-2021, Thomas Maier-Komor
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

function compile_file() {
	$WFC -fno-genlib -t $TARGET $1.wfc
	if [ "$?" != "0" ]; then
		echo WFC: compiling $1 failed
		exit 1
	fi

	update_h=0
	if [ -f main/$1.h ]; then
		diffsize=`diff $1.h main/$1.h|wc -l`
		if [ "$diffsize" -gt "4" ]; then
			#echo $1.h is out-of-date
			update_h=1
		fi
	else
		update_h=1
	fi

	update_cpp=0
	if [ -f main/$1.cpp ]; then
		diffsize=`diff $1.cpp main/$1.cpp|wc -l`
		if [ "$diffsize" -gt "4" ]; then
			#echo $1.cpp is out-of-date
			update_cpp=1
		fi
	else
		update_cpp=1
	fi

	if [ "$update_h" == "1" ]; then
		echo updating main/$1.h
		mv $1.h main
	else
		echo $1.h is up-to-date
		rm $1.h
	fi

	if [ "$update_cpp" == "1" ]; then
		echo updating main/$1.cpp
		mv $1.cpp main
	else
		echo $1.cpp is up-to-date
		rm $1.cpp
	fi
}

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
if [[ "$v" < "R2104.4" ]]; then
	echo WFC is too old. Please update WFC.
	exit 1
fi

compile_file hwcfg
compile_file swcfg

$WFC -t $TARGET -l swcfg.wfc
if [ "$?" != "0" ]; then
	echo WFC generating lib failed
	exit 1
fi

update_h=0
if [ -f main/wfccore.h ]; then
	diffsize=`diff wfccore.h main/wfccore.h|wc -l`
	if [ "$diffsize" -gt "4" ]; then
		#echo wfccore.h is out-of-date
		update_h=1
	fi
else
	update_h=1
fi

update_cpp=0
if [ -f main/wfccore.cpp ]; then
	diffsize=`diff wfccore.cpp main/wfccore.cpp|wc -l`
	if [ "$diffsize" -gt "4" ]; then
		#echo wfccore.cpp is out-of-date
		update_cpp=1
	fi
else
	update_cpp=1
fi

if [ "$update_h" == "1" ]; then
	echo updating main/wfccore.h
	mv wfccore.h main
else
	echo wfccore.h is up-to-date
	rm wfccore.h
fi

if [ "$update_cpp" == "1" ]; then
	echo updating main/wfccore.cpp
	mv wfccore.cpp main
else
	echo wfccore.cpp is up-to-date
	rm wfccore.cpp
fi
exit 0
