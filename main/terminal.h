/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


class Terminal
{
	public:
	explicit Terminal(bool crnl = false, uint8_t plvl = 0)
	: m_error(0)
	, m_crnl(crnl)
	, m_plvl(plvl)
	{ }

	virtual ~Terminal() = 0;

	// formatted print with nl->crnl translation
	virtual void printf(const char *, ...);
	virtual void vprintf(const char *, va_list);

	// print with nl->crnl translation
	virtual void print(const char *s, size_t = 0);

	// bare metal write
	virtual int write(const char *, size_t) = 0;
	
	// bare metal read minimum 1 character
	virtual int read(char *, size_t, bool block = true) = 0;

	virtual void disconnect()
	{ }

	virtual int get_ch(char*);

	uint8_t getPrivLevel() const
	{ return m_plvl; }

	void setPrivLevel(uint8_t p)
	{ m_plvl = p; }

	int readInput(char *buf, size_t s, bool echo);

	const char *error() const
	{ return m_error; }

	protected:
	const char *m_error;

	private:
	bool m_crnl;
	uint8_t	m_plvl;
};


inline Terminal::~Terminal()
{

}


#endif
