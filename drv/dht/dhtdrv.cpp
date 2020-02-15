/*
 *  Copyright (C) 2018-2020, Thomas Maier-Komor
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

#include <sdkconfig.h>

#include "dhtdrv.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <rom/ets_sys.h>

#ifdef ESP8266
#include <esp8266/timer_register.h>
#endif

#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>



#ifdef ESP32
#define gettime() esp_timer_get_time()
#elif defined ESP8266
static inline uint64_t gettime()
{
       static uint32_t last = 0, hw = 0;
       uint32_t now = REG_READ(FRC2_COUNT_ADDRESS);
       if (now < last)
               ++hw;
       last = now;
       return (((uint64_t)hw) << 32) | ((uint64_t)now);
}
#elif defined ESP8266 && IDF_VERSION >= 32
#define gettime() soc_get_ticks()
#elif defined CLOCK_MONOTONIC
long gettime()
{
	struct timespec ts;
	int x = clock_gettime(CLOCK_MONOTONIC,&ts);
	assert(x == 0);
	return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
#else
#include <sys/time.h>
long gettime()
{
	struct timeval tv;
	int x = gettimeofday(&tv,0);
	assert(x == 0);
	return tv.tv_sec * 1000000 + tv.tv_usec;
}

#endif

#ifdef ESP8266
// time in FRC2-ticks (guesed values)
#define DT_MIN         250
#define DT_MAX         950
#define DT_THRESH      500
// time in soc ticks (heuristic values)
//#define DT_MIN       4000
//#define DT_MAX       13000
//#define DT_THRESH    9500
#else
// times in microseconds (according to DHT spec)
#define DT_MIN         50
#define DT_MAX         150
#define DT_THRESH      100
#endif

// define to get calibration data
//#define CALIBRATION

#if CALIBRATION
uint16_t Edges[40], *Edge = Edges;
#endif

void DHT::fallIntr(void *arg)
{
	DHT *d = (DHT*)arg;
	long now = gettime();
	long dt = now - d->m_lastEdge;
	d->m_lastEdge = now;
	++d->m_edges;
#ifdef CALIBRATION
	if (d->m_bit < 40)
		*Edge++ = dt;
#endif
	if ((d->m_bit >= 40) || (dt < DT_MIN) || (dt > DT_MAX)) {
		++d->m_errors;
	} else {
		uint8_t bit = d->m_bit;
		if (dt > DT_THRESH)  // this is a 1
			d->m_data[bit/8] |= 1<<((39-bit)&7);
		d->m_bit = ++bit;
	}
}


DHT::DHT(uint8_t pin, DHTModel_t model)
: m_model(model)
, m_error(0)
, m_ready(false)
, m_pin(pin)
{

}


DHT::~DHT()
{
	if (m_error)
		free(m_error);
	gpio_isr_handler_remove((gpio_num_t)m_pin);
}


bool DHT::begin(void)
{
#ifdef ESP32
	if (esp_err_t e = gpio_reset_pin((gpio_num_t)m_pin)) {
		setError("unable to reset pin %u: %s",m_pin,esp_err_to_name(e));
		return false;
	}
#endif
	if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)m_pin,fallIntr,(void*)this)) {
		setError("unable to add isr hander: %s",esp_err_to_name(e));
		return false;
	}
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)m_pin,GPIO_INTR_NEGEDGE)) {
		setError("unable to set intr type: %s",esp_err_to_name(e));
		return false;
	}
	if (esp_err_t e = gpio_set_direction((gpio_num_t)m_pin, GPIO_MODE_INPUT)) {
		setError("cannot set gpio %u as input: %s",m_pin,esp_err_to_name(e));
		return false;
	}
	if (esp_err_t e = gpio_pullup_en((gpio_num_t)m_pin)) {
		setError("cannot activate pull-up on %u: %s",m_pin,esp_err_to_name(e));
		return false;
	}
	// sampling should be possible right after setup
	m_lastReadTime = gettime()/1000 - getMinimumSamplingPeriod();
	clearError();
	m_ready = true;
	return true;
}


void DHT::setError(const char *fmt, ...)
{
	if (m_error)
		free(m_error);
	va_list val;
	va_start(val,fmt);
	vasprintf(&m_error,fmt,val);
	va_end(val);
}


void DHT::clearError()
{
	if (m_error) {
		free(m_error);
		m_error = 0;
	}
}


// Returns true if a reading attempt was made (successfull or not)
bool DHT::read(bool force)
{
	if (!m_ready) {
		setError("driver not ready");
		return false;
	}
	// don't read more than every getMinimumSamplingPeriod() milliseconds
	unsigned long currentTime = gettime()/1000;
	if (!force && ((currentTime - m_lastReadTime) < getMinimumSamplingPeriod())) {
		setError("sampling too early");
		return false;
	}

	// reset lastReadTime, temperature and humidity
	m_lastReadTime = currentTime;
	memset(m_data,0,sizeof(m_data));

	// send start signal
	if (esp_err_t e = gpio_set_direction((gpio_num_t)m_pin, GPIO_MODE_OUTPUT)) {
		setError("unable to set output: %s",esp_err_to_name(e));
		return false;
	}
	if (esp_err_t e = gpio_set_level((gpio_num_t)m_pin,0)) {
		setError("unable to set low level: %s",esp_err_to_name(e));
		return false;
	}
#ifdef CALIBRATION
	Edge = Edges;
#endif

	if (m_model == DHT_MODEL_DHT11)
		vTaskDelay(18); // [18-20]ms
	else
		ets_delay_us(800); // [0.8-20]ms

	// start reading the data line
	if (esp_err_t e =  gpio_set_direction((gpio_num_t)m_pin, GPIO_MODE_INPUT)) {
		setError("unable to set gpio %u to input: %s",m_pin,esp_err_to_name(e));
		return false;
	}
	if (esp_err_t e = gpio_pullup_en((gpio_num_t)m_pin)) {
		setError("unable to enable pull-up on gpio %u: %s",m_pin,esp_err_to_name(e));
		return false;
	}

#ifdef ESP32
	// esp32 does not work without this delay
	// esp8266 does not need an explicit delay
	ets_delay_us(150);
#endif
	m_bit = 0;
	m_edges = 0;
	m_errors = 0;
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)m_pin,GPIO_INTR_NEGEDGE)) {
		setError("error setting interrupt type to negative edge: %s",esp_err_to_name(e));
		return false;
	}
	vTaskDelay(200);
	if (esp_err_t e = gpio_set_intr_type((gpio_num_t)m_pin,GPIO_INTR_DISABLE)) {
		setError("cannot disable interrupts: %s",esp_err_to_name(e));
		return false;
	}
#ifdef CALIBRATION
	// ignore Edge[0], it will always be out-of-range, and gets ignored
	/*
	ESP_LOGD(TAG,"%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u"
			,Edges[0],Edges[1],Edges[2],Edges[3],Edges[4],Edges[5],Edges[6],Edges[7]
			,Edges[8],Edges[9],Edges[10],Edges[11],Edges[12],Edges[13],Edges[14],Edges[15]
			);
	*/
	for (int i = 0; i < sizeof(Edges)/sizeof(Edges[0]); ++i) {
		if (Edges[i] < DT_MIN)
			setError("edge %u: time %u too low",i,Edges[i]);
		else `if (Edges[i] > DT_MAX)
			setError("edge %u: time %u too high",i,Edges[i]);
	}
#endif

	//ESP_LOGD(TAG,"0: %ld-%ld = %ld",FallTimes[1],FallTimes[0],FallTimes[1]-FallTimes[0]);
	if (m_errors > 1)	// start impulse has unexpected timing
		setError("got %u edges with unexpected timing",m_errors-1);
	if (m_bit != 40) {
		setError("got %u edges instead of expected 40",m_edges);
		return false;
	}

	// verify checksum
	if (m_data[4] != ((m_data[0] + m_data[1] + m_data[2] + m_data[3]) & 0xFF)) {
		setError("checksum error: %x + %x + %x + %x = %x, expected %x"
			,m_data[0],m_data[1],m_data[2],m_data[3]
			,m_data[0]+m_data[1]+m_data[2]+m_data[3]
			,m_data[4]);
		return false;
	}

	// we made it
	clearError();
	return true;
}


float DHT::getTemperature() const
{
	float t;
	switch (m_model) {
	case DHT_MODEL_DHT11:
		t = m_data[2];
		break;
	case DHT_MODEL_DHT22:
	case DHT_MODEL_DHT21:
		t = (m_data[2] & 0x7F) << 8 | m_data[3];
		t *= 0.1;
		if (m_data[2] & 0x80)
			t *= -1;
		break;
	default:
		return NAN;
	}
	return t;
}


float DHT::getHumidity() const
{
	float h;
	switch (m_model) {
	case DHT_MODEL_DHT11:
		h = m_data[0];
		break;
	case DHT_MODEL_DHT22:
	case DHT_MODEL_DHT21:
		h = m_data[0] << 8 | m_data[1];
		h *= 0.1;
		break;
	default:
		return NAN;
	}
	return h;
}


int16_t DHT::getRawTemperature() const
{
	int16_t t;
	switch (m_model) {
	case DHT_MODEL_DHT11:
		return m_data[2];
	case DHT_MODEL_DHT22:
	case DHT_MODEL_DHT21:
		t = (m_data[2] & 0x7F) << 8 | m_data[3];
		if (m_data[2] & 0x80)
			t *= -1;
		return t;
	default:
		return 0;
	}
}


uint16_t DHT::getRawHumidity() const
{
	uint16_t h;
	switch (m_model) {
	case DHT_MODEL_DHT11:
		h = m_data[0];
		break;
	case DHT_MODEL_DHT22:
	case DHT_MODEL_DHT21:
		h = m_data[0] << 8 | m_data[1];
		break;
	default:
		return 0;
	}
	return h;
}


