/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#ifndef JTAG_TERMINAL_H
#define JTAG_TERMINAL_H

#ifdef __cplusplus
#include "terminal.h"

class JtagTerminal : public Terminal
{
	public:
	explicit JtagTerminal(bool crle = false);

	const char *type() const override
	{ return "jtag"; }

	int read(char *, size_t, bool = true) override;
	int write(const char *b, size_t) override;
};

#endif // __cplusplus


#endif
