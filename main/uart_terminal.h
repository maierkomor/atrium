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

#ifndef UART_TERMINAL_H
#define UART_TERMINAL_H

#ifdef __cplusplus
#include "terminal.h"
#include <stdint.h>

class UartTerminal : public Terminal
{
	public:
	explicit UartTerminal(uint8_t, bool = false, uint16_t = 100);
	~UartTerminal();

	void init();
	int read(char *, size_t, bool = true);
	int write(const char *b, size_t);
	void set_baudrate(unsigned);

	private:
	uint16_t m_delay;
	uint8_t m_uart;
};

extern "C"
#endif // __cplusplus
void termserv_setup();


#endif
