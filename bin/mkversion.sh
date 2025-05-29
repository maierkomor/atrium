#!/bin/bash

#
#  Copyright (C) 2022-2025, Thomas Maier-Komor
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

PROJECT_ROOT=${PROJECT_ROOT:-`pwd`}
VERSION_H_NEW=`mktemp`
#echo PROJECT_ROOT=$PROJECT_ROOT
if [ -f $PROJECT_ROOT/.hg_archival.txt ]; then
	# Gather versions information from .hg_archival.txt for archives.
	TAG=`awk '/^tag:/ {printf("%s",$2);}' .hg_archival.txt`
	LTAG=`awk '/^latesttag:/ {printf("%s",$2);}' .hg_archival.txt`
	TAGD=`awk '/^latesttagdistance:/ {printf("%s",$2);}' .hg_archival.txt`
	if [ "" != "$TAG" ]; then
		VER=$TAG
	elif [ "" = "$LTAG" ]; then
		echo archive has no tag or latesttag
		exit 1
	elif [ "0" != $TAGD ]; then
		VER=$LTAG.$TAGD
	else
		VER=$LTAG
	fi
	echo "#define ATRIUM_VERSION \"$VER\"" > $VERSION_H_NEW
	BR=`awk '/^branch:/ {printf("%s",$2);}' .hg_archival.txt`
	echo "#define HG_BRANCH \"$BR\"" >> $VERSION_H_NEW
elif [ -d $PROJECT_ROOT/.hg ]; then
	# Gather versions information from repository and sandbox.
	eval `hg log -r. -T"TAGS={tags} HG_ID={node|short} HG_BRANCH={branch} HG_REV={rev} TAGV={latesttag}{if(latesttagdistance,'.{latesttagdistance}')}"`
	VER=$TAGS
	if [ "tip" = "$VER" ]; then
		# Then create versions based on latest tag, tag distance, and modification indicator.
		VER=$TAGV
	elif [ "" = "$VER" ]; then
		VER=$TAGV
	fi
	# Check if we have modified, removed, added or deleted files.
	hg_st=`hg st -mard`
	if [ "$hg_st" != "" ]; then
		# add delta indicator
		VER+="+"
	fi
	VER+=" (hg:$HG_REV/$HG_ID)"
	echo "#define ATRIUM_VERSION \"$VER\"" > $VERSION_H_NEW
	echo "#define HG_ID \"$HG_ID\"" >> $VERSION_H_NEW
	echo "#define HG_REV \"$HG_REV\"" >> $VERSION_H_NEW
	echo "#define HG_BRANCH \"$HG_BRANCH\"" >> $VERSION_H_NEW
elif [ -d $PROJECT_ROOT/.git ]; then
	VER=`git describe --tags --long --dirty`
	echo "#define ATRIUM_VERSION \"$VER\"" > $VERSION_H_NEW
else
	# Bail out with an error, if no versions information an be gathered.
	echo no versions information available
	exit 1
fi

if [ "$#" = "0" ]; then
	echo "Version $VER"
	exit 0
fi

VERSION_H=$1
#echo VERSION_H=$VERSION_H

if [ -d "$IDF_PATH" ]; then
	pushd "$IDF_PATH" > /dev/null
	IDF_VER=`git describe --tags 2>/dev/null | sed 's/\.//;s/v//;s/-.*//;s/\..*//'`
	popd > /dev/null
fi

if [ "$IDF_VER" != "" ]; then
	echo "#define IDF_VERSION $IDF_VER" >> $VERSION_H_NEW
fi

atrium_ver_new=`mktemp`
echo "$VER" > $atrium_ver_new
echo versions $VER

if [ ! -f data/version.txt ]; then
	echo creating data/version.txt
	cp $atrium_ver_new data/version.txt
else
	cmp data/version.txt $atrium_ver_new 2>&1 > /dev/null
	if [ "0" = "$?" ]; then
		echo data/version.txt is up-to-date
	else
		echo updating data/version.txt
		cp -f $atrium_ver_new data/version.txt
	fi
fi

if [ -d "$BUILD_DIR" ]; then
	if [ ! -f "$BUILD_DIR/version.txt" ]; then
		echo creating $BUILD_DIR/version.txt
		cp $atrium_ver_new "$BUILD_DIR/version.txt"
	else
		cmp "$BUILD_DIR/version.txt" $atrium_ver_new 2>&1 > /dev/null
		if [ "0" = "$?" ]; then
			echo $BUILD_DIR/version.txt is up-to-date
		else
			echo updating $BUILD_DIR/version.txt
			cp -f $atrium_ver_new "$BUILD_DIR/version.txt"
		fi
	fi
else
	echo "build directory not set"
fi
rm $atrium_ver_new

if [ ! -d `dirname $VERSION_H` ]; then
	mkdir -p `dirname $VERSION_H` || exit 1
fi

if [ ! -f "$VERSION_H" ]; then
	echo creating $VERSION_H
	mv -f $VERSION_H_NEW "$VERSION_H"
else
	cmp $VERSION_H $VERSION_H_NEW 2>&1 > /dev/null
	if [ "0" = "$?" ]; then
		echo versions.h is up-to-date
		rm $VERSION_H_NEW
	else
		echo updating $VERSION_H
		mv -f $VERSION_H_NEW "$VERSION_H"
	fi
fi 
exit 0
