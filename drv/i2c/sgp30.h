/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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

#ifndef SGP30_H
#define SGP30_H

#include "i2cdrv.h"
#include "env.h"

#define SGP30_ADDR	(0x58<<1)

class EnvNumber;


struct SGP30 : public I2CDevice
{
	explicit SGP30(uint8_t port);
	SGP30(uint8_t port, uint8_t addr, const char *n);

	static SGP30 *create(uint8_t bus);

	const char *drvName() const override;
	int init();
	uint8_t status();
	void attach(class EnvObject *);
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	float calc_press(int32_t adc_P, int32_t t_fine);
	int32_t calc_tfine(int32_t adc_T);
	static unsigned cyclic(void *arg);
	unsigned cyclic();
	int updateHumidity();
	int init_airq();
	int sample();
	int read();
	int selftest_start();
	int selftest_finish();
	int get_serial();
	int get_version();
	int read_serial();
	int read_version();

	typedef enum { st_none, st_init, st_bist, st_gets, st_getv, st_readb, st_readd
		, st_reads, st_readv, st_iaq, st_idle, st_update, st_measure, st_error
	} state_t;
	enum { f_ver = 1, f_ser = 2, f_bist = 4, f_iaq = 8 };

	EnvNumber m_tvoc, m_co2;	// owned
	EnvNumber *m_temp = 0, *m_humid = 0;	// refered to
	uint16_t m_ahumid = 0xffff, m_ver = 0xffff;
	state_t m_state = st_none;
	esp_err_t m_err = 0;
	uint8_t m_serial[6];
	uint8_t m_flags = 0;
};


unsigned sgp30_scan(uint8_t);


#endif
