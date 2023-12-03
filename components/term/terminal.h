/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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

#include "stream.h"
#include "estring.h"


class Terminal : public stream
{
	public:
	explicit Terminal(bool crnl = false, uint8_t plvl = 0)
	: stream(crnl)
	, m_error(0)
	, m_plvl(plvl)
	{ }

	~Terminal() override;

	virtual const char *type() const
	{ return "unnamed"; }

	// bare metal read minimum 1 character
	virtual int read(char *, size_t, bool block = true)
	{ return -1; }

	virtual int disconnect()
	{ return 0; }

	virtual int get_ch(char*);

	uint8_t getPrivLevel() const
	{ return m_plvl; }

	void setPrivLevel(uint8_t p)
	{ m_plvl = p; }

	int readInput(char *buf, size_t s, bool echo);

	const char *error() const
	{ return m_error; }

	const estring &getPwd();
	int setPwd(const char *pwd);

	virtual bool isInteractive() const
	{ return true; }

	void print_hex(const uint8_t *b, size_t s, size_t off = 0);

	protected:
	const char *m_error = 0;
	estring m_pwd;

	private:
	Terminal(const Terminal &);
	Terminal &operator = (const Terminal &);
	uint8_t	m_plvl;
	bool m_synced = true;
};


class NullTerminal : public Terminal
{
	public:
	NullTerminal()
	: Terminal()
	{ }

	const char *type() const override
	{ return "null"; }

	// bare metal write
	int write(const char *, size_t n) override
	{ return n; }
	
	// bare metal read minimum 1 character
	int read(char *, size_t, bool block = true) override
	{ return 0; }
};

/*
typedef enum cmd_ret_e {
	RET_OK = 0,
	RET_INV_ARG_1,
	RET_INV_ARG_2,
	RET_INV_ARG_3,
	RET_INV_ARG_4,
	RET_INV_ARG_5,
	RET_INV_ARG_6,
	RET_INV_ARG_7,
	RET_INV_ARG_8,
	RET_INV_ARG_9,
	RET_INV_ARG_10,
	RET_INV_ARG_11,
	RET_INV_ARG_12,
	RET_INV_ARG_13,
	RET_INV_ARG_14,
	RET_INV_ARG_15,
	RET_FAILED,
	RET_RANGE,
	RET_MISSING_ARG,
	RET_INV_NUMARG,
	RET_INVARG,
	RET_OOM,		// out of memory
	RET_PERM,		// access denied
	RET_TIMEOUT,
	RET_NOT_FOUND,
	RET_NOT_CONNECTED,
	RET_ERRNO,
} cmd_ret_t;
*/


#endif
