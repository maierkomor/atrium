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

#ifndef DHTDRV_H
#define DHTDRV_H

#include <math.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class EnvObject;
class EnvNumber;


// DHTModel_t
typedef enum {
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

	DHTModel_t m_model;
	EnvNumber *m_temp = 0, *m_humid = 0;
	unsigned long m_lastReadTime;
	long m_lastEdge;
	SemaphoreHandle_t m_mtx;
	uint8_t m_data[5];
	uint8_t m_bit, m_edges, m_errors;
	uint16_t m_start;
	bool m_ready, m_error;

	protected:
	uint8_t m_pin;
};


#endif
