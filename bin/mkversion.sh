#!/bin/bash

PROJECT_ROOT=${PROJECT_ROOT:-`pwd`}
VERSION_NEW=`mktemp`
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
	echo "#define ATRIUM_VERSION \"$VER\"" > $VERSION_NEW
	BR=`awk '/^branch:/ {printf("%s",$2);}' .hg_archival.txt`
	echo "#define HG_BRANCH \"$BR\"" >> $VERSION_NEW
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
	echo "#define ATRIUM_VERSION \"$VER\"" > $VERSION_NEW
	echo "#define HG_ID \"$HG_ID\"" >> $VERSION_NEW
	echo "#define HG_REV \"$HG_REV\"" >> $VERSION_NEW
	echo "#define HG_BRANCH \"$HG_BRANCH\"" >> $VERSION_NEW
elif [ -d $PROJECT_ROOT/.git ]; then
	VER=`git describe --tags --long --dirty`
	echo "#define ATRIUM_VERSION \"$VER\"" > $VERSION_NEW
else
	# Bail out with an error, if no versions information an be gathered.
	echo no versions information available
	exit 1
fi

if [ "$#" = "0" ]; then
	echo "$VER"
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
	echo "#define IDF_VERSION $IDF_VER" >> $VERSION_NEW
fi

version_new=`mktemp`
echo "$VER" > $version_new
echo versions $VER

if [ ! -f data/version.txt ]; then
	echo creating data/version.txt
	cp $version_new data/version.txt
else
	cmp data/version.txt $version_new 2>&1 > /dev/null
	if [ "0" = "$?" ]; then
		echo data/version.txt is up-to-date
	else
		echo updating data/version.txt
		cp -f $version_new data/version.txt
	fi
fi
if [ -d "$BUILD_DIR" ]; then
	if [ ! -f "$BUILD_DIR/version.txt" ]; then
		echo creating $BUILD_DIR/version.txt
		cp $version_new "$BUILD_DIR/version.txt"
	else
		cmp "$BUILD_DIR/version.txt" $version_new 2>&1 > /dev/null
		if [ "0" = "$?" ]; then
			echo $BUILD_DIR/version.txt is up-to-date
		else
			echo updating $BUILD_DIR/version.txt
			cp -f $version_new "$BUILD_DIR/version.txt"
		fi
	fi
fi
rm $version_new

if [ ! -d `dirname $VERSION_H` ]; then
	mkdir -p `dirname $VERSION_H` || exit 1
fi

if [ ! -f "$VERSION_H" ]; then
	echo creating $VERSION_H
	mv -f $VERSION_NEW "$VERSION_H"
else
	cmp $VERSION_H $VERSION_NEW 2>&1 > /dev/null
	if [ "0" = "$?" ]; then
		echo versions.h is up-to-date
		rm $VERSION_NEW
	else
		echo updating $VERSION_H
		mv -f $VERSION_NEW "$VERSION_H"
	fi
#elif cmp "$VERSION_H" "$VERSION_NEW" 2>&1 /dev/null; then
#	echo versions.h is up-to-date
#	rm $VERSION_NEW
#else
#	echo updating $VERSION_H
#	mv -f $VERSION_NEW "$VERSION_H"
fi 
exit 0
