#!/bin/bash

#
#  Copyright (C) 2018-2023, Thomas Maier-Komor
#  Atrium Distribution for ESP
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

xtoolsdir=""
idfdir=""
interactive="1"
overwrite="0"
settings=`mktemp`
installdir=`pwd`/idf
patchdir=`dirname $(readlink -f $0)`/patches

while getopts "d:fnh" opt; do
	case ${opt} in
	d)
		installdir=`realpath $OPTARG`
		if [ -d $installdir ]; then
			echo will install to $installdir
		elif [ -e $installdir ]; then
			echo $OPTARG is not a directory
		else
			echo creating $installdir
			mkdir $installdir || exit 1
		fi
		;;
	f)
		overwrite="1"
		;;
	n)
		interactive="0"
		;;
	h)
		echo "setup all tools necessary for Atrium"
		echo "-n      : to run non-interactively"
		echo "-d <dir>: download and install missing tools/IDF to directory <dir>"
		echo "-f      : to overwrite an existing settings.mk file"
		echo "-h      : display this help"
		exit
		;;
	esac
done

cmds="git make wget g++ flex bison gperf pip"

#echo $cmds
IFS=' '
for i in $cmds
do
	which $i > /dev/null
	if [ $? == "1" ]; then
		echo please install $i
		exit 1
	else
		echo found $i
	fi
done

if [ ! -e /usr/include/md5.h ]; then
	echo please install libmd-dev package with md5.h and libmd
	exit 1
fi

if [ "$interactive" != "0" ]; then
	echo This script will download crosstools, SDKs and WFC to a directory given with option -d, if needed.
	echo Press enter to continue or CTRL-C to abort.
	echo Install directory is $installdir
	read
fi

if [ -e settings.mk ]; then
	if [ "$overwrite" == "0" ]; then
		echo settings.mk already exists. Please remove it and restart.
		exit 1
	fi
fi

#if [ "$XTOOLS_ESP32" != "" ]; then
#	PATH=$PATH:$XTOOLS_ESP32/bin
#fi
#
#if [ "$XTOOLS_ESP8266" != "" ]; then
#	PATH=$PATH:$XTOOLS_ESP8266/bin
#fi

if [ ! -d $installdir ]; then
	echo creating install direcotry $installdir
	mkdir -p $installdir
fi

## esp8266 crosstools
#ESP8266_GCC=`which xtensa-lx106-elf-gcc`
#if [ "$?" == "1" ]; then
#	# look if it is already installed
#	ESP8266_GCC="$installdir/xtensa-lx106-elf/bin/xtensa-lx106-elf-gcc"
#	if [ ! -x "$ESP8266_GCC" ]; then
#		ESP8266_GCC=""
#	fi
#fi
#if [ "" == "$ESP8266_GCC" ]; then
#	if [ "$installdir" == "" ]; then
#		echo ESP8266 crosstools missing: xtensa-lx106-elf-gcc could not be found.
#		echo Please download, install, and adjust path manually or use option -d to trigger download.
#		exit 1
#	fi
#	pushd $installdir
#	if [ `uname -m` == "x86_64" ]; then
#		xtools=xtensa-lx106-elf-linux64-1.22.0-100-ge567ec7-5.2.0.tar.gz
#	else
#		xtools=xtensa-lx106-elf-linux32-1.22.0-100-ge567ec7-5.2.0.tar.gz
#	fi
#	if [ "1" == "$interactive" ]; then
#		echo OK to start download of esp8266 xtools? Press CTRL-C to cancel.
#		read
#	else
#		echo starting download of $xtools
#	fi
#	wget https://dl.espressif.com/dl/$xtools || exit 1
#	tar xf $xtools || exit 1
#	rm $xtools
#	popd > /dev/null
#	XTOOLS_ESP8266="$installdir/xtensa-lx106-elf/bin"
#else
#	XTOOLS_ESP8266=`dirname $ESP8266_GCC`
#	echo ESP8266 xtools found at $XTOOLS_ESP8266
#fi
#XTOOLS_ESP8266=`dirname $XTOOLS_ESP8266`
#echo "# xtools for esp8266" >> $settings
#echo "XTOOLS_ESP8266=$XTOOLS_ESP8266" >> $settings
#PATH=$PATH:$XTOOLS_ESP8266

## esp8266 crosstools
#ESP32_GCC=`which xtensa-esp32-elf-gcc`
#if [ "$?" == "1" ]; then
#	ESP32_GCC="$installdir/xtensa-esp32-elf/bin/xtensa-esp32-elf-gcc"
#	if [ ! -x "$ESP32_GCC" ]; then
#		ESP32_GCC=""
#	fi
#fi
#if [ "" == "$ESP32_GCC" ]; then
#	if [ "$installdir" == "" ]; then
#		echo ESP32 crosstools missing: xtensa-esp32-elf-gcc could not be found.
#		echo Please download, install, and adjust path manually or use option -d to trigger download.
#		exit 1
#	fi
#	pushd $installdir
#	if [ `uname -m` == "x86_64" ]; then
#		xtools=xtensa-esp32-elf-linux64-1.22.0-96-g2852398-5.2.0.tar.gz
#	else
#		xtools=xtensa-esp32-elf-linux32-1.22.0-96-g2852398-5.2.0.tar.gz
#	fi
#	if [ "1" == "$interactive" ]; then
#		echo OK to start download of esp32 xtools? Press CTRL-C to cancel.
#		read
#	else
#		echo starting download of $xtools
#	fi
#	wget https://dl.espressif.com/dl/$xtools || exit 1
#	tar xf $xtools || exit 1
#	rm $xtools
#	popd > /dev/null
#	XTOOLS_ESP32="$installdir/xtensa-esp32-elf/bin"
#else
#	XTOOLS_ESP32=`dirname $ESP32_GCC`
#	echo ESP32 xtools found at $XTOOLS_ESP32
#fi


echo =======
echo ESP8266
echo =======

## ESP8266 IDF
if [ "$IDF_ESP8266" == "" ]; then
	if [ -d "$installdir/idf-esp8266" ]; then
		IDF_ESP8266="$installdir/idf-esp8266"
	fi
fi
if [ "$IDF_ESP8266" == "" ]; then
	if [ "$installdir" == "" ]; then
		echo unable to find ESP8266 IDF
		exit 1
	fi
	if [ "1" == "$interactive" ]; then
		echo OK to start download of esp8266 IDF? Press CTRL-C to cancel.
		read
	else
		echo starting download of esp8266 IDF
	fi
	pushd $installdir > /dev/null
	git clone https://github.com/espressif/ESP8266_RTOS_SDK.git idf-esp8266 || exit 1
	cd idf-esp8266
	IDF_ESP8266=`pwd`
	popd > /dev/null
fi

pushd "$IDF_ESP8266"
git pull --recurse-submodule
git reset --hard v3.3 || exit 1
git submodule deinit -f --all
git switch release/v3.3
git submodule update --init
echo install tools
python2 -m pip install --upgrade pip
IDF_PATH="$IDF_ESP8266" python2 tools/check_python_dependencies.py
IDF_PATH=`pwd` python2 tools/idf_tools.py install-python-env || exit 1
IDF_PATH=`pwd` python2 tools/idf_tools.py install || exit 1
popd > /dev/null

echo settings for $IDF_ESP8266
pushd "$IDF_ESP8266"
echo IDF_ESP8266=$IDF_ESP8266 >> $settings
IDF_PATH="$IDF_ESP8266" python2 "tools/idf_tools.py" export | sed "s/export //g;s/=/_ESP8266=/g;s/;/\n/g;s/\"//g" >> $settings
popd > /dev/null
echo checking requirements of ESP8266 IDF in $IDF_ESP8266
#IDF_PATH=$IDF_ESP8266 python2 -m pip install --user -r requirements.txt

echo ===================
cat $settings
echo ===================
echo

pushd "$IDF_ESP8266"
echo "updating to latest v3.3"
git checkout v3.3
echo patching IDF for ESP8266
patch -t -p1 < $patchdir/idf-esp8266-v3.3.diff || echo PATCHING FAILED!
popd

## ESP32 IDF
if [ "$IDF_ESP32" == "" ]; then
	if [ -d "$installdir/idf-esp32" ]; then
		IDF_ESP32="$installdir/idf-esp32"
	fi
fi
if [ "$IDF_ESP32" == "" ]; then
	if [ "$installdir" == "" ]; then
		echo unable to find ESP32 IDF
		exit 1
	fi
	if [ "1" == "$interactive" ]; then
		echo OK to start download of esp32 IDF? Press CTRL-C to cancel.
		read
	else
		echo starting download of esp32 IDF
	fi
	pushd $installdir > /dev/null
	git clone https://github.com/espressif/esp-idf.git idf-esp32 || exit 1
	cd idf-esp32
	IDF_ESP32=`pwd`
	popd > /dev/null
fi
pushd $IDF_ESP32
git pull --recurse-submodule
git reset --hard v5.1.2
git submodule deinit -f --all
git submodule update --init
IDF_PATH="$IDF_ESP32" bash install.sh
IDF_PATH="$IDF_ESP32" python3 tools/idf_tools.py install
#echo patching IDF for ESP32
#patch -t -p1 < $patchdir/idf-esp32-v5.1.diff || echo PATCHING FAILED!
echo patching lwip of ESP32
cd components/lwip/lwip
patch -t -p1 < $patchdir/esp32-lwip-v5.1.diff || echo PATCHING FAILED!
popd > /dev/null

echo ===================
cat $settings
echo ===================

echo settings for $IDF_ESP32
echo IDF_ESP32=$IDF_ESP32 >> $settings
pushd "$IDF_ESP32"
source export.sh
IDF_PATH="$IDF_ESP32" python3 "$IDF_ESP32/tools/idf_tools.py" export | sed "s/export //g;s/=/_ESP32=/g;s/;/\n/g;s/\"//g" >> $settings
echo >> $settings
popd > /dev/null

#
# WFC is still used, but files are generated with a private version
#
### WFC - wire format compiler
#WFC=`which wfc`
#if [ "$?" != "0" ]; then
#	if [ -x "$installdir/wfc/bin/wfc" ]; then
#		WFC="$installdir/wfc/bin/wfc"
#	fi
#fi
#if [ "$WFC" == "" ]; then
#	if [ "1" == "$interactive" ]; then
#		echo OK to start download of WFC? Press CTRL-C to cancel.
#		read
#	else
#		echo starting download of WFC
#	fi
#	pushd $installdir
#	git clone https://github.com/maierkomor/wfc.git wfc || exit 1
#	cd wfc
#	./configure || exit 1
#	make || exit 1
#	WFC=`pwd`/bin/wfc
#	popd > /dev/null
#	echo "PATH=`basename wfc`:\$PATH" >> $settings
#fi
#if [ -x $WFC ]; then
#	WFCDIR=`dirname $WFC`/..
#	WFCDIR=`readlink -f $WFCDIR`
#else
#	echo failed to find or install WFC
#	exit 1
#fi
#	
#echo found WFC at $WFCDIR
#PATH=$PATH:$WFCDIR/bin
#
#echo "# WFC" >> $settings
#echo "WFCDIR=$WFCDIR" >> $settings
#echo "WFC=$WFC" >> $settings

#echo "# PATH"
#echo PATH=$PATH >> $settings

## Webcam sources
#if [ ! -e "esp32/camera/.git/config" ]; then
#	mkdir -p esp32
#	pushd esp32
#	echo $interactive
#	if [ "1" == "$interactive" ]; then
#		echo OK to start download of esp32-camera? Press CTRL-C to cancel.
#		read
#	else
#		echo starting download of esp32 camera package
#	fi
#	git clone https://github.com/espressif/esp32-camera camera
#else
#	echo updating camera package
#	pushd esp32/camera
#	git pull || exit 1
#fi
#popd > /dev/null


# python modules
#echo checking requirements of ESP32 IDF in $IDF_ESP32
#IDF_PATH=$IDF_ESP32 pip install -r $IDF_ESP32/requirements.txt
#
#pip install -U pip
#echo checking requirements of ESP8266 IDF in $IDF_ESP8266
#IDF_PATH=$IDF_ESP8266 pip install -r $IDF_ESP8266/requirements.txt


cp $settings settings.sh || exit 1
mv $settings settings.mk || exit 1
sed -i 's/:$PATH//' settings.mk
echo ============================================================
echo new settings:
echo ============================================================
cat settings.mk
