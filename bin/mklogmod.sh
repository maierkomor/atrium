#!/bin/bash
#
# Copyright (C) 2021-2023, Thomas Maier-Komor
# Atrium Firmware Package for ESP
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

mod_h=components/logging/modules.h
mod_c=components/logging/modules.c

modules=`mktemp`

find drv components main -name '*.c' -o -name '*.cpp' | 
	xargs awk -F' ' '/#define TAG MODULE_/ {gsub("MODULE_","",$3); print tolower($3)}' | 
	sort -u > $modules

cat - > $mod_h << ====
/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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

extern const char ModNames[];
extern const uint16_t ModNameOff[];

====

cat - > $mod_c << ====
/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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

const char ModNames[] =
====

printf "\t\"<undef>\\\\0" >> $mod_c
declare -A modoff
size=8
while read -r mod; do
	printf '%s\\%n0' $mod s >> $mod_c
	modoff[$mod]=$size
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

// enum definitions
typedef enum logmod_e {
	logmod_invalid = 0,
====
x=1
while read -r mod; do
	printf '\tlogmod_%s,\n' $mod
	let x=x+1
done < $modules >> $mod_h

cat - >> $mod_h << ====
} logmod_t;

// module defines
====

x=0
while read -r line; do
	let x=x+1
	m=`echo $line | tr \[:lower:\] \[:upper:\]`
	printf '#define MODULE_%-15s %s\n' $m logmod_$line
done < $modules >> $mod_h
printf '#define MAX_MODULE_ID          %3u\n' $x >> $mod_h
let x=x+1
printf '#define NUM_MODULES            %3u\n' $x >> $mod_h


cat - >> $mod_h << ====

#ifdef USE_MODULE
#define TAG USE_MODULE
#endif

#define MODULE_NAME (ModNames+ModNameOff[TAG])

#endif
====

rm $modules
