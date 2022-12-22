#!/bin/bash

#
#  Copyright (C) 2021, Thomas Maier-Komor
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

function checkage() {
	if [ $dirty -eq 0 ]; then
		if [ components/wfc/$1.cpp -ot $2.wfc ]; then
			#echo components/wfc/$1.cpp may be outdated
			dirty=1
		elif [ components/wfc/$1.h -ot $2.wfc ]; then
			#echo components/wfc/$1.h may be outdated
			dirty=1
		fi
	fi
}

if [ -f $SDKCONFIG ]; then
	TARGET=`grep CONFIG_WFC_TARGET "$SDKCONFIG" | sed 's/.*="//;s/"//'`
elif [ -f "projects/$PROJECT" ]; then
	TARGET=`grep CONFIG_WFC_TARGET "projects/$PROJECT" | sed 's/.*="//;s/"//'`
fi

if [ "$TARGET" == "" ]; then
	TARGET=$CHIP
fi
echo sym-linking wfc generated sources for target $TARGET, project $PROJECT

if [ ! -f components/wfc/hwcfg_$TARGET.cpp ]; then
	echo unsupported target $TARGET
	exit 1
fi

dirty=0

checkage hwcfg_$TARGET hwcfg
checkage swcfg_$TARGET swcfg
checkage wfccore_$TARGET swcfg
checkage wfccore_$TARGET hwcfg

if [ $dirty -ne 0 ]; then
	if [ -d .hg ]; then
		which hg > /dev/null
		if [ $? -eq 0 ]; then
			hgst=`hg status -m *.wfc`
			if [ $? -eq 0 -a "$hgst" == "" ]; then
				dirty=0
				echo hg says wfc files are clean
			fi
		fi
	fi
	if [ -d .git ]; then
		which git > /dev/null
		if [ $? -ne 0 ]; then
			git diff-index --quiet HEAD *.wfc
			if [ $? -eq 0 ]; then
				dirty=0
				echo git says wfc files are clean
			fi
		fi
	fi
	if [ $dirty -ne 0 ]; then
		echo Warning: WFC generated files may be outdated!
	fi
else
	echo WFC generated files are up-to-date
fi

ln -f -s ../components/wfc/hwcfg_$TARGET.cpp main/hwcfg.cpp
ln -f -s ../components/wfc/hwcfg_$TARGET.h main/hwcfg.h
ln -f -s ../components/wfc/swcfg_$TARGET.cpp main/swcfg.cpp
ln -f -s ../components/wfc/swcfg_$TARGET.h main/swcfg.h
ln -f -s ../components/wfc/wfccore_$TARGET.cpp main/wfccore.cpp
ln -f -s ../components/wfc/wfccore_$TARGET.h main/wfccore.h
