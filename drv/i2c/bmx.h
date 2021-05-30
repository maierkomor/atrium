/*
 *  Copyright (C) 2018-2021, Thomas Maier-Komor
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

#include "i2cdrv.h"
#include "bme680.h"

class JsonNumber;


struct BMP280 : public I2CSensor
{
	BMP280(uint8_t port, uint8_t addr)
	: I2CSensor(port,addr,drvName())
	{ }

	BMP280(uint8_t port, uint8_t addr, const char *n)
	: I2CSensor(port,addr,n)
	{ }

	const char *drvName() const
	{ return "bmp280"; }

	int init();
	void attach(class JsonObject *);
	virtual unsigned cyclic();

	protected:
	float calc_press(int32_t adc_P, int32_t t_fine);
	int32_t calc_tfine(uint8_t *);
	void init_calib(uint8_t *);
	static void trigger(void *);
	bool status();
	virtual bool sample();
	virtual bool read();
	virtual void handle_error();

	JsonNumber *m_temp = 0, *m_press = 0;
	uint16_t T1 = 0;
	int16_t T2 = 0, T3 = 0;
	uint16_t P1 = 0;
	int16_t P2 = 0, P3 = 0, P4 = 0, P5 = 0, P6 = 0, P7 = 0, P8 = 0, P9 = 0;
	typedef enum { st_idle, st_sample, st_measure, st_read } state_t;
	state_t m_state = st_idle;
};


struct BME280 : public BMP280
{
	BME280(uint8_t port, uint8_t addr)
	: BMP280(port,addr,drvName())
	{ }

	const char *drvName() const
	{ return "bme280"; }

	int init();
	void attach(class JsonObject *);

	protected:
	float calc_humid(int32_t adc_H, int32_t t_fine);
	bool sample();
	bool read();
	void handle_error();

	JsonNumber *m_humid = 0;
	uint8_t H1 = 0, H3 = 0;
	int16_t H2 = 0, H4 = 0, H5 = 0;
	int8_t H6 = 0;
};


struct BME680 : public I2CSensor
{
	BME680(uint8_t port, uint8_t addr)
	: I2CSensor(port,addr,drvName())
	{ }

	const char *drvName() const
	{ return "bme680"; }

	int init();
	void attach(class JsonObject *);

	protected:
	unsigned sample();
	unsigned read();
	static unsigned cyclic(void *);
	static void trigger(void *);

	JsonNumber *m_temp = 0, *m_press = 0, *m_humid = 0, *m_gas = 0;
	bme680_dev m_dev;
	typedef enum { st_idle, st_sample, st_read, st_error } state_t;
	state_t m_state = st_idle;
	bool m_sample = false;
};


#endif