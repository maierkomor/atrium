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

#include "ledcluster.h"
#include "log.h"

static const char TAG[] = "ledc";

LedCluster *LedCluster::Instance = 0;

LedCluster::LedCluster()
: m_next(Instance)
{
	Instance = this;
}


int LedCluster::write(uint8_t d)
{
	return -1;
}

int LedCluster::write(uint8_t *d, unsigned n)
{
	log_dbug(TAG,"write(*,%u)",n);
//	if (off >= 0)
//		setOffset(off);
	while (n) {
		if (write(*d))
			return 1;
		++d;
		--n;
	}
	return 0;
}

uint8_t LedCluster::maxDim() const 
{
	return UINT8_MAX;
}


int LedCluster::setOn(bool)
{
	return -1;
}
