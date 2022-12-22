#!/bin/bash

# check settings
if [ ! -f settings.sh ]; then
	echo settings.sh not found. Please run setupenv.sh.
	exit 1
fi
source settings.sh

# look for project config
if [ ! -f projects/$1 ]; then
	echo Unable to find project config.
	exit 1
fi

# setup build dir
export BUILD_DIR=build.$1
if [ ! -d "$BUILD_DIR" ]; then
	mkdir -p "$BUILD_DIR"
fi

# source settings
export SDKCONFIG=`pwd`/projects/$1
export SDKCONFIG_DEFAULTS=$SDKCONFIG
source $SDKCONFIG
export CONFIG_INTEGRATED_HELP


if [ "$CONFIG_IDF_TARGET_ESP32" == "y" ]; then
	export IDF_PATH=$IDF_ESP32
	export ESP_FAM=32
	export IDF_TARGET=esp32
elif  [ "$CONFIG_IDF_TARGET_ESP32S2" == "y" ]; then
	export IDF_PATH=$IDF_ESP32
	export ESP_FAM=32
	export IDF_TARGET=esp32s2
elif  [ "$CONFIG_IDF_TARGET_ESP32S3" == "y" ]; then
	export IDF_PATH=$IDF_ESP32
	export ESP_FAM=32
	export IDF_TARGET=esp32s3
elif  [ "$CONFIG_IDF_TARGET_ESP32C3" == "y" ]; then
	export IDF_PATH=$IDF_ESP32
	export ESP_FAM=32
	export IDF_TARGET=esp32c3
elif  [ "$CONFIG_IDF_TARGET_ESP8266" == "y" ]; then
	make PROJECT=$1 $2
	exit
else
	echo Unknown or invalid IDF_TARGET.
	exit 1
fi

if [ ! -d "$IDF_PATH" ]; then
	echo IDF_PATH is not a directory.
	exit 1
fi

pushd $IDF_PATH
IDF_VERSION=`git describe --tags 2>/dev/null | sed 's/\.//;s/v//;s/-.*//;s/\..*//'`
export IDF_VERSION
popd > /dev/null

IDF_PY=$IDF_PATH/tools/idf.py
if [ ! -x $IDF_PY ]; then
	echo Unable to find idf.py. Please source $IDF_PATH/export.sh.
	exit 1
fi

eval `python3 $IDF_PATH/tools/idf_tools.py export`

bash bin/link_wfc.sh || exit 1
bin/genmemfiles.sh || exit 1
bin/mkversion.sh main/versions.h || exit 1

if  [ "$CONFIG_IDF_TARGET_ESP32" == "y" ]; then
	IDF_TARGET=esp32
elif  [ "$CONFIG_IDF_TARGET_ESP32S2" == "y" ]; then
	IDF_TARGET=esp32s2
elif  [ "$CONFIG_IDF_TARGET_ESP32S3" == "y" ]; then
	IDF_TARGET=esp32s3
elif  [ "$CONFIG_IDF_TARGET_ESP32C3" == "y" ]; then
	IDF_TARGET=esp32c3
fi

ATRIUM_VER=`cat "$BUILD_DIR/version.txt"`

if [ "$2" != "" ]; then
	$IDF_PY -B "$BUILD_DIR" -DIDF_TARGET=$IDF_TARGET -DSDKCONFIG=$SDKCONFIG "-DPROJECT_VER=$ATRIUM_VER" $2
else
	$IDF_PY -B "$BUILD_DIR" -DIDF_TARGET=$IDF_TARGET -DSDKCONFIG=$SDKCONFIG "-DPROJECT_VER=$ATRIUM_VER" build
fi
