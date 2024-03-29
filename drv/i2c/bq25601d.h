/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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

#ifndef BQ25601D_H
#define BQ25601D_H

#include <sdkconfig.h>
#include "charger.h"
#include "i2cdrv.h"
#include "env.h"
#include "event.h"


struct BQ25601D : public I2CDevice
#ifdef CONFIG_ESP_PHY_ENABLE_USB
		  , public Charger
#endif
{
	BQ25601D(uint8_t port, uint8_t addr, const char *n = 0);

	void addIntr(uint8_t gpio) override;
	void attach(class EnvObject *) override;

	const char *drvName() const override
	{ return "bq25601d"; }

	static BQ25601D *scan(uint8_t);

#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif
	int getImax();
	int setImax(unsigned imax);

	protected:
	static void intrHandler(void *arg);
	static void processIntr(void *arg);
	static void powerDown(void *arg);
	void processIntr();
	static unsigned cyclic(void *);
	unsigned cyclic();
	EnvBool m_pg;
	EnvString m_charge, m_vbus;
	unsigned m_irqcnt = 0;
	uint8_t m_regs[0xb];
	uint8_t m_wdcnt = 0;
	event_t m_onev = 0, m_offev = 0;
};


#endif
