#!/bin/bash

PROJECT_ROOT=${PROJECT_ROOT:-`pwd`}
#echo PROJECT_ROOT=$PROJECT_ROOT

if [ -f $PROJECT_ROOT/.hg_archival.txt ]; then
	# Gather version information from .hg_archival.txt for archives.
	TAG=`awk '/^tag:/ {printf("%s",$2);}' .hg_archival.txt`
	LTAG=`awk '/^latesttag:/ {printf("%s",$2);}' .hg_archival.txt`
	TAGD=`awk '/^latesttagdistance:/ {printf("%s",$2);}' .hg_archival.txt`
	if [ "" != "$TAG" ]; then
		VER=$TAG
	elif [ "" == "$LTAG" ]; then
		echo archive has no tag or latesttag
		exit 1
	elif [ "0" != $TAGD ]; then
		VER=$LTAG.$TAGD
	else
		VER=$LTAG
	fi
	echo "#define VERSION \"$VER\"" > version.h.new
	BR=`awk '/^branch:/ {printf("%s",$2);}' .hg_archival.txt`
	echo "#define HG_BRANCH \"$BR\"" >> version.h.new
elif [ -d $PROJECT_ROOT/.hg ]; then
	# Gather version information from repository and sandbox.
	eval `hg log -r. -T"TAGS={tags} HG_ID={node|short} HG_BRANCH={branch} HG_REV={rev} TAGV={latesttag}{if(latesttagdistance,'.{latesttagdistance}')}"`
	VER=$TAGS
	if [ "tip" == "$VER" ]; then
		# Then create version based on latest tag, tag distance, and modification indicator.
		VER=$TAGV
	fi
	# Check if we have modified, removed, added or deleted files.
	if [ `hg st -mard | wc -l` != "0" ]; then
		# add delta indicator
		VER+="+"
	fi
	VER+=" (hg:$HG_REV/$HG_ID)"
	echo "#define VERSION \"$VER\"" > version.h.new
	echo "#define HG_ID \"$HG_ID\"" >> version.h.new
	echo "#define HG_REV \"$HG_REV\"" >> version.h.new
	echo "#define HG_BRANCH \"$HG_BRANCH\"" >> version.h.new
elif [ -d $PROJECT_ROOT/.git ]; then
	VER=`git describe --tags --long --dirty`
	echo "#define VERSION \"$VER\"" > version.h.new
else
	# Bail out with an error, if no version information an be gathered.
	echo no version information available
	exit 1
fi
echo "$VER" > version.txt.new

echo version $VER

if [ ! -f data/version.txt ]; then
	mv version.txt.new data/version.txt
else
	cmp data/version.txt version.txt.new 2>&1 > /dev/null
	if [ "0" == "$?" ]; then
		echo version.txt is up-to-date
		rm version.txt.new
	else
		echo updating version.txt
		mv -f version.txt.new data/version.txt
	fi
fi

COMPONENT_PATH=${COMPONENT_PATH:-$PROJECT_ROOT/main}
if [ ! -f $COMPONENT_PATH/version.h ]; then
	echo creating version.h
	mv version.h.new $COMPONENT_PATH/version.h
else
	cmp $COMPONENT_PATH/version.h version.h.new 2>&1 > /dev/null
	if [ "0" == "$?" ]; then
		echo version.h is up-to-date
		rm version.h.new
	else
		echo updating version.h
		mv -f version.h.new $COMPONENT_PATH/version.h
	fi
fi 
exit 0
