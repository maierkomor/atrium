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

#ifndef DRV_MAX7219_H
#define DRV_MAX7219_H

#include "ledcluster.h"
#include "xio.h"

//#include <driver/spi.h> -- for ESP8266???

//
// 5V level adjustment necessary 
//

class MAX7219Drv : public LedCluster
{
	public:
	static MAX7219Drv *create(xio_t clk, xio_t dout, xio_t cs, bool odrain);

	int clear();
	uint8_t getDim() const
	{ return m_intensity; }
	uint8_t maxDim() const
	{ return UINT8_MAX; }
	void setDigits(uint8_t);
	int setDim(uint8_t);
	int setOn(bool);
	int setNumDigits(unsigned);
	int setPos(uint8_t x, uint8_t y) override;
	int write(uint8_t v) override;
	void setDecoding(uint8_t);
	void displayTest(bool);

	const char *drvName() const override
	{ return "max7219"; }

	private:
	MAX7219Drv(xio_t clk, xio_t dout, xio_t cs);

	void setreg(uint8_t r, uint8_t v);

	xio_t m_clk, m_dout, m_cs;
	uint8_t m_digits[8];
	uint8_t m_at = 0, m_intensity = 0, m_ndigits = 8;
};


#endif
