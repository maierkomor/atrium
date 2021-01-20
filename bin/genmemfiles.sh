#!/bin/bash

#
#  Copyright (C) 2018-2020, Thomas Maier-Komor
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

# needed to get name of array right
cd data

if [ ! -d $ldir ]; then
	mkdir -p $ldir
fi

echo "#ifndef MEMFILES_H" > memfiles.h.new
echo "#define MEMFILES_H" >> memfiles.h.new
echo >> memfiles.h.new
for i in $MEMFILES; do
	file="$i"
	filename=`basename "$i"`
	if [ "$CONFIG_INTEGRATED_HELP" = "y" ]; then
		echo "extern char $(echo $filename | sed 's/\./_/g')[];" >> memfiles.h.new
		echo "#define $(echo $filename | sed 's/\./_/g')_len $(stat --printf='%s' $i)" >> memfiles.h.new
	else
		echo "#define $(echo $filename | sed 's/\./_/g') 0" >> memfiles.h.new
	fi
	cpp_file=`echo $filename|sed 's/\.html/_html.cpp/;s/\.man/_man.cpp/'`
	#echo comparing $file against $ldir/$cpp_file
	test -e "$ldir/$cpp_file"
	ex=$?
	test "$file" -nt "$ldir/$cpp_file"
	nt=$?
	if [ "$ex" == "1" ] ||  [ "$nt" == "0" ]; then
		echo updating $ldir/$cpp_file
		pushd `dirname $i`
		xxd -i $filename "$ldir/$cpp_file"
		popd > /dev/null
		sed -i 's/^unsigned //;' "$ldir/$cpp_file"
		sed -i 's/}/ ,0x00}/' "$ldir/$cpp_file"
		sed -i 's/int .*;//' "$ldir/$cpp_file"
#	else
#		echo $ldir/$cpp_file is up-to-date
	fi
	shift
done

echo >> memfiles.h.new
echo "#endif" >> memfiles.h.new

if [ -e memfiles.h.new -a -e $ldir/memfiles.h ]; then
	diff memfiles.h.new $ldir/memfiles.h 2>&1 > /dev/null
	x="$?"
else
	x="1"
fi
if [ "$x" == "0" ]; then
	echo memfiles.h is up-to-date
	rm memfiles.h.new
else
	echo updating memfiles.h
	if [ -e memfiles.h ]; then
		rm memfiles.h
	fi
	mv memfiles.h.new $ldir/memfiles.h
fi
