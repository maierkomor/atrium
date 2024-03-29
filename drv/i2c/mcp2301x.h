/*
 *  Copyright (C) 2022-2024, Thomas Maier-Komor
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

#ifndef MCP2301X_H
#define MCP2301X_H

#include "i2cdrv.h"
#include "event.h"
#include "xio.h"


class MCP2301X : public XioCluster, public I2CDevice
{
	public:
	static MCP2301X *create(uint8_t, uint8_t, int8_t inta = -1, int8_t intb = -1);
	static MCP2301X *atAddr(uint8_t);

	// x-io-cluster
	event_t get_fallev(uint8_t io) override;
	event_t get_riseev(uint8_t io) override;
	int get_lvl(uint8_t io) override;
	int set_hi(uint8_t io) override;
	int set_lo(uint8_t io) override;
	int setm(uint32_t values,uint32_t mask) override;
	int set_intr_a(xio_t a) override;
	int set_intr_b(xio_t a) override;
	int config(uint8_t io, xio_cfg_t) override;
	int set_lvl(uint8_t io, xio_lvl_t v) override;
	const char *getName() const override;
	int get_dir(uint8_t num) const override;
	unsigned numIOs() const override
	{ return 16; }

	// I2CDevice
	const char *drvName() const override;
	void attach(class EnvObject *) override;

	private:
	MCP2301X(uint8_t bus, uint8_t addr, int8_t inta, int8_t intb);

	int get_dir(uint8_t io);
	int get_in(uint16_t *p);
	int get_pending(uint16_t *p);
	int set_dir(uint8_t io, xio_cfg_io_t dir);
	int set_intr(uint8_t io, xio_cfg_intr_t intr);
	int set_out(uint16_t p);
	int set_pullups(uint16_t pullups);
	int set_pullup(uint8_t io, xio_cfg_pull_t pull);
	int set_reg_bit(uint8_t reg, uint8_t bit, bool value);
	void setPolarity(uint16_t);
	void setPullup(uint16_t);
	void setIntEn(uint16_t);
	int setGpio(uint8_t gpio, bool v);
	int getGpio(uint8_t gpio);
	int getDir(uint8_t gpio);
	static void eval_intrA(void *);
	static void eval_intrB(void *);

	static MCP2301X *Instances;

	MCP2301X *m_next = 0;
	uint8_t m_bus, m_addr;
	uint16_t m_dir = 0;
	event_t m_ibev = 0;
	xio_intrhdlr_t m_hdlra = 0, m_hdlrb = 0;
	void *m_intrarga = 0, *m_intrargb = 0;
	event_t m_fallev[16] = {0}, m_riseev[16] = {0};
};


#endif
