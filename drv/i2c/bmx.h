/*
 *  Copyright (C) 2018-2023, Thomas Maier-Komor
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

#ifndef BMXDRV_H
#define BMXDRV_H

#include "bme680.h"
#include "env.h"
#include "i2cdrv.h"


struct BMP280 : public I2CDevice
{
	BMP280(uint8_t port, uint8_t addr, const char *n = 0);

	const char *drvName() const override
	{ return "bmp280"; }

	int init() override;
	void attach(class EnvObject *) override;
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	float calc_press(int32_t adc_P, int32_t t_fine);
	int32_t calc_tfine(uint8_t *);
	static void trigger(void *);
	bool status();
	virtual int sample();
	virtual int read();
	virtual void handle_error();
	static unsigned cyclic(void *);

	EnvNumber m_temp, m_press;
	uint16_t T1 = 0;
	int16_t T2 = 0, T3 = 0;
	uint16_t P1 = 0;
	int16_t P2 = 0, P3 = 0, P4 = 0, P5 = 0, P6 = 0, P7 = 0, P8 = 0, P9 = 0;
	uint8_t H1 = 0;
	typedef enum { st_idle, st_sample, st_measure, st_read } state_t;
	state_t m_state = st_idle;
	// bit 7-5: 500ms interval in normal mode
	// bit 4-2: IIR filter off
	// bit 1,0: power-off
	uint8_t m_cfg = 0b10000000;
	// bit 7-5: temperature oversampling:	001=1x, ... 101=16x
	// bit 4-2: pressure oversampling:	001=1x, ... 101=16x
	// bit 1,0: sensor mode			00: sleep, 01/10: force, 11: normal
	uint8_t m_sampmod = 0b00100101;
};


struct BME280 : public BMP280
{
	BME280(uint8_t port, uint8_t addr);

	const char *drvName() const
	{ return "bme280"; }

	int init();
	void attach(class EnvObject *);
#ifdef CONFIG_I2C_XCMD
	const char *exeCmd(struct Terminal &, int argc, const char **argv) override;
#endif

	protected:
	float calc_humid(int32_t adc_H, int32_t t_fine);
	int sample();
	int read();
	void handle_error();

	EnvNumber m_humid;
	int16_t H2 = 0, H4 = 0, H5 = 0;
	int8_t H3 = 0, H6 = 0;
};


struct BME680 : public I2CDevice
{
	BME680(uint8_t port, uint8_t addr);

	const char *drvName() const
	{ return "bme680"; }

	int init();
	void attach(class EnvObject *);

	protected:
	unsigned sample();
	unsigned read();
	static unsigned cyclic(void *);
	static void trigger(void *);

	EnvNumber m_temp, m_press, m_humid, m_gas;
	bme680_dev m_dev;
	typedef enum { st_idle, st_sample, st_read, st_error } state_t;
	state_t m_state = st_idle;
	bool m_sample = false;
};


#endif
