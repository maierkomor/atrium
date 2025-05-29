#!/bin/bash

#
#  Copyright (C) 2018-2025, Thomas Maier-Komor
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

if [ "$BATCH_BUILD" == "1" ]; then
	echo BATCH_BUILD set - skipping generation
	exit
fi

ldir=`pwd`/components/memfiles

# needed to get name of array right
cd data

if [ ! -d $ldir ]; then
	mkdir -p $ldir
fi

MEMFILES=`ls -1 man/*.man`
MEMFILES_H=`mktemp`
CMAKELISTS=`mktemp`

cat > $MEMFILES_H << EOF
#ifndef MEMFILES_H
#define MEMFILES_H

#ifdef CONFIG_IDF_TARGET_ESP8266
#define ROMSTR __attribute__((section(".flash.text")))
#else
#define ROMSTR
#endif

#ifdef CONFIG_INTEGRATED_HELP
EOF

cat > $CMAKELISTS << EOF
set(COMPONENT_ADD_INCLUDEDIRS .)
set(COMPONENT_SRCS
EOF

for i in $MEMFILES; do
	file="$i"
	filename=`basename "$i"`
	fn_nodot=`echo $filename | sed 's/\./_/g'`
	cpp_file=`printf "%s.cpp" $fn_nodot`
	size=`stat --printf='%s' $i`
	printf "extern const char ROMSTR $fn_nodot""[];\n#define $fn_nodot""_len $size\n" >> $MEMFILES_H
	test ! -e "$ldir/$cpp_file" -o "$file" -nt "$ldir/$cpp_file"
	if [ "$?" == "0" ]; then
		echo updating $ldir/$cpp_file
		cat << EOHEADER > "$ldir/$cpp_file"
#include <sdkconfig.h>
#include "memfiles.h"

#ifdef CONFIG_INTEGRATED_HELP
EOHEADER
		pushd `dirname $i`
		xxd -i "$filename" >> "$ldir/$cpp_file"
		popd > /dev/null
		sed -i 's/unsigned char/const char ROMSTR/;s/]/]/;s/^unsigned //;s/}/ ,0x00}/;s/int .*;//' "$ldir/$cpp_file"
		cat << EOFOOTER >> "$ldir/$cpp_file"
#else
#define $fn_nodot 0
#endif
EOFOOTER
	fi
	echo "	$filename" | sed 's/.man/_man.cpp/' >> $CMAKELISTS
	shift
done

printf "\n#else // no CONFIG_INTEGRATED_HELP\n\n" >> $MEMFILES_H

for i in $MEMFILES; do
	file="$i"
	filename=`basename "$i"`
	fn_nodot=`echo $filename | sed 's/\./_/g'`
	printf "#define $fn_nodot 0\n#define ${fn_nodot}_len 0\n" >> $MEMFILES_H
	shift
done

printf "\n\n#endif // CONFIG_INTEGRATED_HELP\n\n#endif\n" >> $MEMFILES_H
printf ")\nregister_component()" >> $CMAKELISTS

if [ -e "$ldir/memfiles.h" ]; then
	#echo diff "$MEMFILES_H" "$ldir/memfiles.h"
	diff -q "$MEMFILES_H" "$ldir/memfiles.h" > /dev/null
	x="$?"
else
	x="1"
fi
if [ "$x" == "0" ]; then
	echo memfiles.h is up-to-date
	rm $MEMFILES_H
	if [ ! -f "$ldir/CMakeLists.txt" ]; then
		mv $CMAKELISTS "$ldir/CMakeLists.txt"
	else
		rm $CMAKELISTS
	fi
else
	echo updating memfiles.h
	if [ -e $ldir/memfiles.h ]; then
		rm $ldir/memfiles.h
	fi
	mv $MEMFILES_H "$ldir/memfiles.h"
	mv $CMAKELISTS "$ldir/CMakeLists.txt"
fi
