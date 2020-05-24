/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

#ifndef TERMSTREAM_H
#define TERMSTREAM_H

#include "stream.h"

class Terminal;


class TermStream : public stream
{
	public:
	TermStream(Terminal &t)
	: stream()
	, term(t)
	{ }

	TermStream &operator << (unsigned short);
	TermStream &operator << (signed short);
	TermStream &operator << (unsigned);
	TermStream &operator << (signed);
	TermStream &operator << (unsigned long);
	TermStream &operator << (signed long);
	TermStream &operator << (unsigned long long);
	TermStream &operator << (signed long long);
	TermStream &operator << (double);
	TermStream &operator << (const char *);
	TermStream &operator << (signed char);
	TermStream &operator << (unsigned char);
	TermStream &operator << (char);

	void put(char c);

	int printf(const char *, ...);
	void write(const char *s, size_t n);

	private:
	Terminal &term;
};

#endif
