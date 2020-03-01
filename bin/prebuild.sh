#!/bin/bash

if [ "$PROJECT" == "" ]; then
	echo error: no target device set
	exit 1
fi

echo preparing build for device $PROJECT

case "$CHIP" in
"ESP32");;
"ESP8266")
	if [ "$IDF_VER" != "32" ]; then
		echo "OTA builds are only supported with IDF v3.2"
		exit 1
	fi
	;;
*)
	echo "architecture not set or invalid ($CHIP)"
	exit 1
	;;
esac

## project specific build directory
if [ ! -d build.$PROJECT ]; then
	mkdir build.$PROJECT || exit 1
fi

if [ -L build ]; then
	b=`readlink build`
	echo build is $b
	if [ "$b" != "build.$PROJECT" ]; then
		echo linking build directory
		rm build
		ln -s -f build.$PROJECT build || exit 1
	fi
else
	ln -s -f build.$PROJECT build || exit 1
fi

export COMPONENT_PATH=`pwd`/main
bash bin/mkversion.sh
bash bin/genmemfiles.sh
bash bin/compile_wfc.sh
