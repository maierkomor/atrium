/*
 *  Copyright (C) 2024, Thomas Maier-Komor
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

#ifndef ADS111X_H
#define ADS111X_H

#include "env.h"
#include "i2cdrv.h"
#include "event.h"


struct ADS1x1x : public I2CDevice
{
	typedef enum dev_type_e {
		ads1013, ads1014, ads1015,
		ads1113, ads1114, ads1115,
	} dev_type_t;

	static ADS1x1x *create(uint8_t bus, uint8_t addr, dev_type_t t);

	const char *drvName() const override
	{ return "ads1x1x"; }

	int init() override;
	void addIntr(uint8_t intr) override;
	void attach(class EnvObject *) override;
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	ADS1x1x(uint8_t port, uint8_t addr, dev_type_t t);

	static unsigned cyclic(void *);
	static void intrHandler(void *arg);
	static int32_t readConfig(uint8_t bus, uint8_t addr);
	static void readdata(void *arg);
	static void sample(void *arg);
	static void set_ch(void *arg);
	
	int read();
	int setChannel(const char *);
	int setChannel(int8_t a, int8_t b);
	int setContinuous(bool c);
	int setGain(float v);
	int setInterval(long itv);
	int setLo(int16_t v);
	int setHi(int16_t v);
	int setSps(long sps);
	int writeConfig();

	EnvNumber m_ad0, m_v0, m_ad1, m_v1, m_ad2, m_v2, m_ad3, m_v3;
	ADS1x1x *m_next;
	float m_gain = 2048;
	event_t m_isrev = 0;
	uint16_t m_cfg;
	dev_type_t m_type;
	bool m_sample = false, m_wait = false;
};


#endif
