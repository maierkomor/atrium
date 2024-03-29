/*
 *  Copyright (C) 2023-2024, Thomas Maier-Komor
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

#ifndef BMP388_H
#define BMP388_H

#include "env.h"
#include "event.h"
#include "i2cdrv.h"


struct BMP388 : public I2CDevice
{
	BMP388(uint8_t port, uint8_t addr, const char *n = 0);

	const char *drvName() const
	{ return "bmp388"; }

	void addIntr(uint8_t intr) override;
	void attach(class EnvObject *);
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	int init();
	float calc_press(int32_t adc_P, int32_t t_fine);
	void calc_tfine(uint32_t);
	void calc_press(uint32_t);
	int flush_fifo();
	static void trigger(void *);
	bool status();
	virtual int sample();
	virtual int read();
	virtual void handle_error();
	static unsigned cyclic(void *);
	int get_error();
	int get_status();

	EnvNumber m_temp, m_press;
	double D[14];
	typedef enum { st_idle, st_sample, st_measure, st_read } state_t;
	state_t m_state = st_idle;
};


unsigned bmp388_scan(uint8_t port);

#endif
