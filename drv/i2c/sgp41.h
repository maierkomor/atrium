/*
 *  Copyright (C) 2026, Thomas Maier-Komor
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

#ifndef SGP41_H
#define SGP41_H

#include "i2cdrv.h"
#include "env.h"

#define SGP41_ADDR	(0x59<<1)

class EnvNumber;


struct SGP41 : public I2CDevice
{
	explicit SGP41(uint8_t port);
	SGP41(uint8_t port, uint8_t addr, const char *n);

	static SGP41 *create(uint8_t bus);

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
	int reset();
	int selftest_start();
	int selftest_finish();
	int get_serial();
	int read_serial();
	int read_version();
	int start_conditioning();
	int done_conditioning();
	int start_bist();
	int read_bist();
	int trigger_measure();
	int read_measure();
	void getParameters(uint8_t *);
	static int reset(uint8_t bus);

	typedef enum { st_idle = 0, st_tr_reset, st_error
		, st_tr_exe_cond, st_wt_exe_cond
		, st_tr_meas_raw, st_wt_meas_raw
		, st_tr_bist, st_wt_bist
		, st_tr_get_serial, st_wt_get_serial
		, st_tr_heater_off
	} state_t;

	EnvNumber m_voc, m_nox;	// owned
	EnvNumber *m_temp = 0, *m_humid = 0;	// refered to
	uint16_t m_ahumid = 0, m_atemp = 0, m_ver = 0xffff;
	state_t m_state = st_tr_bist;
	uint8_t m_serial[9];
};


unsigned sgp41_scan(uint8_t);


#endif
