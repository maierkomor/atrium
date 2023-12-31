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

if [ "$PROJECT" == "" ]; then
	echo error: no target project set
	exit 1
fi

if [ ! -f "projects/$PROJECT" ]; then
	echo error: unable to find project configuration file projects/$PROJECT
	exit 1
fi

echo preparing build of project $PROJECT

case "$CHIP" in
"ESP32");;
"ESP8266");;
*)
	echo "architecture not set or invalid ($CHIP)"
	exit 1
	;;
esac

rm build 2>&1 > /dev/null
ln -s -f build.$PROJECT build || exit 1
export COMPONENT_PATH=`pwd`/main
bash bin/mkversion.sh main/versions.h || exit 1
bash bin/genmemfiles.sh || exit 1
