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

#ifndef MCP2300X_H
#define MCP2300X_H

#include "i2cdrv.h"
#include "event.h"
#include "xio.h"


class MCP2300X : public XioCluster, public I2CDevice
{
	public:
	static MCP2300X *create(uint8_t, uint8_t, int8_t inta = -1);
	static MCP2300X *atAddr(uint8_t);

	// x-io-cluster
	event_t get_fallev(uint8_t io) override;
	event_t get_riseev(uint8_t io) override;
	int get_lvl(uint8_t io) override;
	int set_hi(uint8_t io) override;
	int set_lo(uint8_t io) override;
	int setm(uint32_t values,uint32_t mask) override;
//	int set_intr(uint8_t,xio_intrhdlr_t,void*) override;
	int set_intr_a(xio_t a) override;
	int config(uint8_t io, xio_cfg_t) override;
	int set_lvl(uint8_t io, xio_lvl_t v) override;
	const char *getName() const override;
	int get_dir(uint8_t num) const override;
	unsigned numIOs() const override
	{ return 8; }

	// I2CDevice
	const char *drvName() const override;
	void attach(class EnvObject *) override;

	private:
	MCP2300X(uint8_t bus, uint8_t addr, int8_t inta);

	int get_dir(uint8_t io);
	int set_dir(uint8_t io, xio_cfg_io_t dir);
	int set_intr(uint8_t io, xio_cfg_intr_t intr);
	int set_pullups(uint8_t pullups);
	int set_pullup(uint8_t io, xio_cfg_pull_t pull);
	int set_reg_bit(uint8_t reg, uint8_t bit, bool value);
	void setPolarity(uint8_t);
	void setPullup(uint8_t);
	void setIntEn(uint8_t);
	int setGpio(uint8_t gpio, bool v);
	int getGpio(uint8_t gpio);
	int getDir(uint8_t gpio);
	static void eval_intr(void *);

	static void intrHandler(void *);
	static MCP2300X *Instances;

	MCP2300X *m_next = 0;
	uint8_t m_bus, m_addr, m_dir = 0;
	event_t m_iev = 0;
	xio_intrhdlr_t m_hdlra = 0, m_hdlrb = 0;
	void *m_intrarga = 0, *m_intrargb = 0;
	event_t m_fallev[8] = {0}, m_riseev[8] = {0};
};


#endif
