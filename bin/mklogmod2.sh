#!/bin/bash

# this requires gcc-esp8266 >= 8.4.0!
# to be used once upgrade of IDF is possible

mod_h=components/logging/modules.h
mod_c=components/logging/modules.c

modules=`mktemp`
nonmods=`mktemp`
modusages=`mktemp`

find main drv components -name '*.cpp' -o -name '*.c' |xargs grep '\<TAG\>' | sed 's/[^:]*\///;s/:.*//'|sort -u>$modusages
find main drv components -name '*.cpp' -o -name '*.c' |xargs grep "^#define USE_MODULE " | sed 's/[^:]*\///;s/:.*//'|sort -u>$nonmods
echo modules.c >> $nonmods
comm -2 -3 $modusages $nonmods > $modules
rm $modusages $nonmods

cat - > $mod_h << ====
/*
 *  Copyright (C) 2021, Thomas Maier-Komor
 *  Atrium Firmware Package for ESP
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MODULES_H
#define MODULES_H

#include <stdint.h>
#include <string.h>
#include <stdexcept>

extern const char ModNames[];
extern const uint16_t ModNameOff[];

====

cat - > $mod_c << ====
/*
 *  Copyright (C) 2021, Thomas Maier-Komor
 *  Atrium Firmware Package for ESP
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "modules.h"

const char *ModuleNames[] = {
	"<undef>",
====

while read -r line; do
	mod=`echo $line|sed 's/\.c.*//'`
	printf '\t"%s",\n' $mod
done < $modules >> $mod_c


cat - >> $mod_c << ====
};

const char ModNames[] =
====
printf "\t%s" '"<undef>\0' >> $mod_c
declare -A modoff
size=8
while read -r line; do
	mod=`echo $line|sed 's/\.c.*//'`
	printf '%s\\%n0' $mod s >> $mod_c
	modoff[$line]=$size
	let size=size+s
done < $modules

cat - >> $mod_c << ====
";

const uint16_t ModNameOff[] = {
	0,
====
while read -r line; do
	printf '\t%u,\t// %s\n' "${modoff[$line]}" $line
done < $modules >> $mod_c
echo "};" >> $mod_c

cat - >> $mod_h << ====

/* requires gcc-esp8266 >= 8.4.0
constexpr bool streq(const char *l, const char *r)
{
	bool ret = false;
	do {
		ret = (*l++ == *r++);
	} while (ret && *l);
	return ret;
}

constexpr int ModuleId(const char *name)
{
	const char *FileNames[] = {
		"",
====

while read -r line; do
	printf '\t\t"%s",\n' $line
done < $modules >> $mod_h


cat - >> $mod_h << ====
	};
	int r = 0;
	for (int i = 0; i < sizeof(FileNames)/sizeof(FileNames[0]); ++i) {
		if (streq(FileNames[i],name)) {
			r = i;
			break;
		}
	}
	return r;
}
*/

// enum definitions
typedef enum logmod_e {
====
x=1
while read -r line; do
	mod=`echo $line|sed 's/\.c.*//'`
	printf '\tlogmod_%-15s= %3u,\n' $mod $x
	let x=x+1
done < $modules >> $mod_h

cat - >> $mod_h << ====
} logmod_t;

// module defines
====

x=0
while read -r line; do
	let x=x+1
	m=`echo $line | sed 's/\.c.*//' | tr \[:lower:\] \[:upper:\]`
	printf '#define MODULE_%-15s %3u\n' $m $x
done < $modules >> $mod_h
printf '#define MAX_MODULE_ID          %3u\n' $x >> $mod_h


cat - >> $mod_h << ====

#ifdef USE_MODULE
#define TAG USE_MODULE
#endif

#ifndef TAG
#define TAG ModuleId(__FILE__)
#endif

#define MODULE_NAME (ModNames+ModNameOff[TAG])

#endif
====

rm $modules
