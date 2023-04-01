/*
 *  Copyright (C) 2021-2022, Thomas Maier-Komor
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

#ifndef TCA9555_H
#define TCA9555_H

#include "i2cdrv.h"
#include "xio.h"


class TCA9555 :
#ifdef CONFIG_IOEXTENDERS
	public XioCluster,
#endif
	public I2CDevice
{
	public:
	static TCA9555 *create(uint8_t,uint8_t);

#ifdef CONFIG_IOEXTENDERS
	int get_lvl(uint8_t io) override;
	int get_out(uint8_t io) override;
	int set_hi(uint8_t io) override;
	int set_lo(uint8_t io) override;
	int set_intr(uint8_t, xio_intrhdlr_t, void*) override;
	int config(uint8_t io, xio_cfg_t) override;
	int set_lvl(uint8_t io, xio_lvl_t v) override;
	const char *getName() const override;
	int get_dir(uint8_t num) const override;
	unsigned numIOs() const override
	{ return 16; }
#endif

	int setGpio(bool v,unsigned off);
	int write(uint16_t);
	int write(uint8_t *v, unsigned n);
	uint16_t read();
	void clear();
	const char *drvName() const override;

	static TCA9555 *getInstance()
	{ return Instance; }

	private:
	TCA9555(uint8_t bus, uint8_t addr)
	: I2CDevice(bus,addr,drvName())
	{ }

	~TCA9555() = default;

	static TCA9555 *Instance;
	TCA9555 *m_next = 0;
	uint16_t m_cfg = 0xffff, m_pol = 0, m_out = 0xffff;
};


#endif

