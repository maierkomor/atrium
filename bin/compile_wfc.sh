#!/bin/bash

if [ "$PROJECT" == "" ]; then
	echo error: target not set
	exit 1
fi

if [ ! -x $WFC ]; then
	echo "unable to find wfc"
	exit 1
fi

TARGET=`grep CONFIG_WFC_TARGET "projects/$PROJECT" | sed 's/.*="//;s/"//'`

if [ "$TARGET" == "" ]; then
	TARGET=$CHIP
fi
echo generating wfc for target $TARGET, project $PROJECT

${WFC:=$WFC_DIR/bin/wfc} -t $TARGET binformats.wfc
if [ "$?" != "0" ]; then
	exit 1
fi

update_h=0
if [ -f main/binformats.h ]; then
	diffsize=`diff binformats.h main/binformats.h|wc -l`
	if [ "$diffsize" -gt "4" ]; then
		echo binformats.h is out-of-date
		update_h=1
	fi
else
	update_h=1
fi

update_cpp=0
if [ -f main/binformats.cpp ]; then
	diffsize=`diff binformats.cpp main/binformats.cpp|wc -l`
	if [ "$diffsize" -gt "4" ]; then
		echo binformats.cpp is out-of-date
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
