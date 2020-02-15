#!/bin/bash
ldir=../components/memfiles

# needed to get name of array right
cd data

if [ ! -d $ldir ]; then
	mkdir -p $ldir
fi

echo "#ifndef MEMFILES_H" > memfiles.h.new
echo "#define MEMFILES_H" >> memfiles.h.new
echo >> memfiles.h.new
for i in *.html; do
	file="$i"
	echo "extern char $(echo $i | sed 's/\./_/g')[];" >> memfiles.h.new
	echo "#define $(echo $i | sed 's/\./_/g')_len $(stat --printf='%s' $i)" >> memfiles.h.new
	cpp_file=`echo $file|sed s'/\.html/_html.cpp/'`
	#echo comparing $file against $ldir/$cpp_file
	test -e "$ldir/$cpp_file"
	ex=$?
	test "$file" -nt "$ldir/$cpp_file"
	nt=$?
	if [ "$ex" == "1" ] ||  [ "$nt" == "0" ]; then
		echo updating $cpp_file
		xxd -i "$file" "$ldir/$cpp_file"
		sed -i 's/^unsigned //;' "$ldir/$cpp_file"
		sed -i 's/}/ ,0x00}/' "$ldir/$cpp_file"
		sed -i 's/int .*;//' "$ldir/$cpp_file"
	else
		echo $cpp_file is up-to-date
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
