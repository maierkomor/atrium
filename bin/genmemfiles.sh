#!/bin/bash

#
#  Copyright (C) 2018-2022, Thomas Maier-Komor
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

ldir=`pwd`/components/memfiles

#echo CONFIG_INTEGRATED_HELP=$CONFIG_INTEGRATED_HELP

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
#define ROMSTR __attribute__((section(".irom0.text.romstr")))
#else
#define ROMSTR
#endif

EOF

cat > $CMAKELISTS << EOF
set(COMPONENT_ADD_INCLUDEDIRS .)
set(COMPONENT_SRCS
EOF

for i in $MEMFILES; do
	file="$i"
	filename=`basename "$i"`
	cpp_file=`echo $filename|sed 's/\.html/_html.cpp/;s/\.man/_man.cpp/'`
	if [ "$CONFIG_INTEGRATED_HELP" = "y" ]; then
		echo "extern const char ROMSTR $(echo $filename | sed 's/\./_/g')[];" >> $MEMFILES_H
		echo "#define $(echo $filename | sed 's/\./_/g')_len $(stat --printf='%s' $i)" >> $MEMFILES_H
		#echo comparing $file against $ldir/$cpp_file
		test -e "$ldir/$cpp_file"
		ex=$?
		test "$file" -nt "$ldir/$cpp_file"
		nt=$?
		if [ "$ex" == "1" ] ||  [ "$nt" == "0" ]; then
			echo updating $ldir/$cpp_file
			pushd `dirname $i`
			xxd -i $filename "$ldir/$cpp_file"
			sed -i 's/unsigned char /#include "memfiles.h"\nconst char ROMSTR /;s/]/]/' "$ldir/$cpp_file"
			popd > /dev/null
			sed -i 's/^unsigned //;' "$ldir/$cpp_file"
			sed -i 's/}/ ,0x00}/' "$ldir/$cpp_file"
			sed -i 's/int .*;//' "$ldir/$cpp_file"
		fi
		echo "	$filename" | sed 's/.man/_man.cpp/' >> $CMAKELISTS
	else
		echo "#define $(echo $filename | sed 's/\./_/g') 0" >> $MEMFILES_H
		rm -f "$ldir/$cpp_file"
	fi
	shift
done

echo >> $MEMFILES_H
echo "#endif" >> $MEMFILES_H
echo ")" >> $CMAKELISTS
echo "register_component()" >> $CMAKELISTS

if [ -e "$ldir/memfiles.h" ]; then
	#echo diff "$MEMFILES_H" "$ldir/memfiles.h"
	diff "$MEMFILES_H" "$ldir/memfiles.h" 2>&1 > /dev/null
	x="$?"
else
	x="1"
fi
if [ "$x" == "0" ]; then
	echo memfiles.h is up-to-date
	rm $MEMFILES_H
	if [ ! -f "$lder/CMakeLists.txt" ]; then
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
