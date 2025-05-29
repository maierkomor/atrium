/*
 *  Copyright (C) 2022-2025, Thomas Maier-Komor
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

#ifndef INA2XX_H
#define INA2XX_H

#include "env.h"
#include "i2cdrv.h"

#define ID_INA219 19
#define ID_INA220 20
#define ID_INA226 26
#define ID_INA236 36
#define ID_INA260 60


struct INA2XX : public I2CDevice
{
	static INA2XX *create(uint8_t port, uint8_t addr, uint8_t type);

	void attach(class EnvObject *) override;
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	INA2XX(uint8_t port, uint8_t addr, uint8_t type, const char *name);
	static unsigned cyclic(void *);
	static void trigger(void *arg);

	static void read(void *);
	static void sample(void *);
	virtual void init();
	virtual unsigned vcyclic();
	void updateDelay();
	virtual void read();
	void sample();
	int reset();
	void setConfig(uint16_t cfg);
	int setMode(bool cur, bool volt, bool cont);
	const char *readReg(uint8_t reg, uint16_t &val);
	const char *writeReg(uint8_t reg, uint16_t val);
	virtual const char *setNumSamples(unsigned n);
	virtual const char *setAlert(uint16_t a, uint16_t l);
	virtual const char *setShunt(float r);
	virtual void writeConfig();
	void updateCal();
	virtual float readBusV();
	virtual float readCurrent();
	virtual bool convReady();
	virtual const char *setCNVR(bool);

	typedef enum mode_e { md_pd = 0, md_tc, md_tv, md_tcv, md_cc, md_cv, md_ccv } mode_t;
	typedef enum state_e { st_off, st_trigger, st_read, st_cont } state_t;

	EnvNumber m_volt, m_amp, m_power;
	Action *m_acrd = 0;
	float m_res = 0, m_Ilsb = 0, m_Blsb = 0, m_Slsb = 0;
	uint16_t m_conf = 0;
	uint16_t m_itv = 10;
	state_t m_st;
	uint8_t m_type;
};

#endif
