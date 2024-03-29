/*
 *  Copyright (C) 2018-2024, Thomas Maier-Komor
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

#ifndef DHTDRV_H
#define DHTDRV_H

#include <math.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "env.h"
#include "xio.h"

class EnvObject;
class EnvNumber;


// DHTModel_t
typedef enum {
	DHT_MODEL_INVALID = 0,
	DHT_MODEL_DHT11 = 11,
	DHT_MODEL_DHT21 = 21,
	DHT_MODEL_DHT22 = 22,
	DHT_MODEL_AM2301 = DHT_MODEL_DHT21, // Packaged DHT21
	DHT_MODEL_AM2302 = DHT_MODEL_DHT22, // Packaged DHT22
	DHT_MODEL_RHT03 = DHT_MODEL_DHT22  // Equivalent to DHT22
} DHTModel_t;


class DHT
{
	public:
	DHT();

	int init(uint8_t pin, uint16_t model);
	float getTemperature() const;
	float getHumidity() const;
	bool read(bool force=false);
	int16_t getRawTemperature() const;
	uint16_t getRawHumidity() const;

	DHTModel_t getModel() const
	{ return m_model; }

	int getMinimumSamplingPeriod()
	{ return m_model == DHT_MODEL_DHT11 ? 1000 : 2000; }

	int8_t getNumberOfDecimalsTemperature()
	{ return m_model == DHT_MODEL_DHT11 ? 0 : 1; }

	int8_t getLowerBoundTemperature()
	{ return m_model == DHT_MODEL_DHT11 ? 0 : -40; }

	int8_t getUpperBoundTemperature()
	{ return m_model == DHT_MODEL_DHT11 ? 50 : 125; }

	int8_t getLowerBoundHumidity()
	{ return m_model == DHT_MODEL_DHT11 ? 20 : 0; }

	int8_t getUpperBoundHumidity()
	{ return m_model == DHT_MODEL_DHT11 ? 90 : 100; }

	void attach(EnvObject *);

	private:
	static void fallIntr(void *arg);

	unsigned long m_lastReadTime = 0;
	long m_lastEdge = 0;
	EnvNumber m_temp, m_humid;
	SemaphoreHandle_t m_mtx;
	DHTModel_t m_model = DHT_MODEL_INVALID;
	uint16_t m_start = 0;
	char m_name[10];
	uint8_t m_data[5];
	uint8_t m_bit = 0, m_edges = 0, m_errors = 0;
	bool m_ready = false, m_error = true;

	protected:
	xio_t m_pin;
};


#endif
