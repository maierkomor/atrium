#!/bin/bash

PROJECT_ROOT=${PROJECT_ROOT:-`pwd`}
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
	echo "#define VERSION \"$VER\"" > versions.h.new
	BR=`awk '/^branch:/ {printf("%s",$2);}' .hg_archival.txt`
	echo "#define HG_BRANCH \"$BR\"" >> versions.h.new
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
	echo "#define VERSION \"$VER\"" > versions.h.new
	echo "#define HG_ID \"$HG_ID\"" >> versions.h.new
	echo "#define HG_REV \"$HG_REV\"" >> versions.h.new
	echo "#define HG_BRANCH \"$HG_BRANCH\"" >> versions.h.new
elif [ -d $PROJECT_ROOT/.git ]; then
	VER=`git describe --tags --long --dirty`
	echo "#define VERSION \"$VER\"" > versions.h.new
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
echo VERSION_H=$VERSION_H

#echo IDF_PATH=$IDF_PATH

if [ -d "$IDF_PATH" ]; then
	pushd "$IDF_PATH" > /dev/null
	IDF_VER=`git describe --tags 2>/dev/null | sed 's/\.//;s/v//;s/-.*//;s/\..*//'`
	popd > /dev/null
fi

if [ "$IDF_VER" != "" ]; then
	echo "#define IDF_VERSION $IDF_VER" >> versions.h.new
fi

echo "$VER" > version.txt.new
echo versions $VER

if [ ! -f data/version.txt ]; then
	echo creating version.txt
	mv -f version.txt.new data/version.txt
else
	cmp data/version.txt version.txt.new 2>&1 > /dev/null
	if [ "0" = "$?" ]; then
		echo version.txt is up-to-date
		rm version.txt.new
	else
		echo updating version.txt
		mv -f version.txt.new data/version.txt
	fi
fi

if [ ! -d `dirname $VERSION_H` ]; then
	mkdir -p `dirname $VERSION_H` || exit 1
fi

if [ ! -f "$VERSION_H" ]; then
	echo creating $VERSION_H
	mv -f versions.h.new "$VERSION_H"
else
	cmp $VERSION_H versions.h.new 2>&1 > /dev/null
	if [ "0" = "$?" ]; then
		echo versions.h is up-to-date
		rm versions.h.new
	else
		echo updating $VERSION_H
		mv -f versions.h.new "$VERSION_H"
	fi
fi 
exit 0
