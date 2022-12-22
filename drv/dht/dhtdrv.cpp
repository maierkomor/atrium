/*
 *  Copyright (C) 2018-2022, Thomas Maier-Komor
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
#include "log.h"
#include "stream.h"
#include "env.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_err.h>

#ifdef ESP8266
#include <rom/gpio.h>
#include <esp8266/timer_register.h>
#include <rom/ets_sys.h>
#elif defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32C3
#if IDF_VERSION >= 40
#include <esp32/rom/ets_sys.h>
#include <esp32/rom/gpio.h>
#else
//| defined CONFIG_IDF_TARGET_ESP32S3 |
#include <rom/ets_sys.h>
#endif
#endif

#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>


#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
#define ENTER_CRITICAL() portDISABLE_INTERRUPTS()
#define EXIT_CRITICAL() portENABLE_INTERRUPTS()
#elif defined CONFIG_IDF_TARGET_ESP8266
#define ENTER_CRITICAL() portENTER_CRITICAL()
#define EXIT_CRITICAL() portEXIT_CRITICAL()
#endif


#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
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


// 0bit: 50us low, 26-28us high
// 1bit: 50us low, 70us high

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
#define DT_THRESH      110
#endif

// define to get calibration data
//#define CALIBRATION

#define TAG MODULE_DHT

#ifdef CALIBRATION
uint16_t Edges[48], *Edge = Edges;
#endif

DHT::DHT()
: m_ready(false)
, m_error(true)
{

}


void DHT::attach(EnvObject *root)
{
	log_info(TAG,"attaching DHT%u at gpio %u",m_model,m_pin);
	char name[16];
	sprintf(name,"dht%u@%u",m_model,m_pin);
	EnvObject *o = root->add(name);
	m_temp = o->add("temperature",NAN,"\u00b0C");
	m_humid = o->add("humidity",NAN,"%");
}

void IRAM_ATTR DHT::fallIntr(void *arg)
{
	DHT *d = (DHT*)arg;
	long now = gettime();
	long dt = now - d->m_lastEdge;
	d->m_lastEdge = now;
	++d->m_edges;
#ifdef CALIBRATION
	if (Edge < (Edges+sizeof(Edges)/sizeof(Edges[0])))
		*Edge++ = dt;
#endif
	if ((dt > DT_MAX) && (d->m_start == 0)) {
		d->m_start = dt;
	} else if ((dt < DT_MIN) || (dt > DT_MAX)) {
		++d->m_errors;
	} else if (d->m_bit < 40) {
		uint8_t bit = d->m_bit;
		if (dt > DT_THRESH)  // this is a 1
			d->m_data[bit/8] |= 1<<((39-bit)&7);
		d->m_bit = ++bit;
	} else {
		++d->m_errors;
	}
}


int DHT::init(uint8_t pin, uint16_t model)
{
	if (model == 2301)
		model = 21;
	else if ((model == 2302) || (model == 3))
		model = 22;
	else if ((model != 11) && (model != 21) && (model != 22)) {
		log_warn(TAG,"unsupported mode");
		return 1;
	}
	m_pin = (xio_t)pin;
	m_model = (DHTModel_t)model;
	m_ready = false;
	m_mtx = xSemaphoreCreateMutex();
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_od;
	cfg.cfg_intr = xio_cfg_intr_disable;
	cfg.cfg_pull = xio_cfg_pull_up;
	if (0 > xio_config(m_pin,cfg)) {
		log_warn(TAG,"error configuring gpio");
		return 2;
	}
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2
	if (esp_err_t e = gpio_reset_pin((gpio_num_t)m_pin)) {
		log_warn(TAG,"reset pin%u: %s",m_pin,esp_err_to_name(e));
		return 2;
	}
#endif
	if (esp_err_t e = gpio_isr_handler_add((gpio_num_t)m_pin,fallIntr,(void*)this)) {
		log_warn(TAG,"gpio%u isr hander: %s",m_pin,esp_err_to_name(e));
		return 3;
	}
	// sampling should be possible right after setup
	m_lastReadTime = gettime()/1000 - getMinimumSamplingPeriod();
	m_error = true;
	m_ready = true;
	return 0;
}


// Returns true if a reading attempt was made (successfull or not)
bool DHT::read(bool force)
{
	if (!m_ready) {
		log_dbug(TAG,"not ready");
		return false;
	}
	// don't read more than every getMinimumSamplingPeriod() milliseconds
	unsigned long currentTime = gettime()/1000;
	if (!force && ((currentTime - m_lastReadTime) < getMinimumSamplingPeriod())) {
		log_dbug(TAG,"too early");
		return false;
	}
	log_dbug(TAG,"read");
	TryLock lock(m_mtx,200);
	if (!lock.locked()) {
		log_dbug(TAG,"lock timeout");
		return false;
	}
	m_error = true;
	if (m_temp)
		m_temp->set(NAN);
	if (m_humid)
		m_humid->set(NAN);

	// reset lastReadTime, temperature and humidity
	m_lastReadTime = currentTime;
	memset(m_data,0,sizeof(m_data));

	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_od;
	ENTER_CRITICAL();
	// send start signal
	xio_config(m_pin,cfg);
	xio_set_lo(m_pin);
#ifdef CALIBRATION
	Edge = Edges;
#endif

	if (m_model == DHT_MODEL_DHT11)
		vTaskDelay(18); // [18-20]ms
	else
		ets_delay_us(1000); // [0.8-20]ms
	m_bit = 0;
	m_edges = 0;
	m_errors = 0;
	m_start = 0;

	// start reading the data line
	xio_set_hi(m_pin);
	cfg.cfg_intr = xio_cfg_intr_fall;
	xio_config(m_pin,cfg);
	EXIT_CRITICAL();
	vTaskDelay(200);
	cfg.cfg_intr = xio_cfg_intr_disable;
	xio_config(m_pin,cfg);
#ifdef CALIBRATION
	// ignore Edge[0], it will always be out-of-range, and gets ignored
	log_dbug(TAG,"%u %u %u %u %u %u %u %u %u %u"
		,Edges[0],Edges[1],Edges[2],Edges[3],Edges[4],Edges[5],Edges[6],Edges[7],Edges[8],Edges[9]
		);
	log_dbug(TAG,"%u %u %u %u %u %u %u %u %u %u"
		,Edges[10],Edges[11],Edges[12],Edges[13],Edges[14],Edges[15],Edges[16],Edges[17],Edges[18],Edges[19]
		);
	log_dbug(TAG,"%u %u %u %u %u %u %u %u %u %u"
		,Edges[20],Edges[21],Edges[22],Edges[23],Edges[24],Edges[25],Edges[26],Edges[27],Edges[28],Edges[29]
		);
	log_dbug(TAG,"%u %u %u %u %u %u %u %u %u %u"
		,Edges[30],Edges[31],Edges[32],Edges[33],Edges[34],Edges[35],Edges[36],Edges[37],Edges[38],Edges[39]
		);
	log_dbug(TAG,"humid %d, temp %d, start %d",m_data[0]<<8|m_data[1],m_data[2]<<8|m_data[3],m_start);
	log_dbug(TAG,"%02x %02x %02x %02x %02x",m_data[0],m_data[1],m_data[2],m_data[3],m_data[4]);
	for (int i = 0; i < sizeof(Edges)/sizeof(Edges[0]); ++i) {
		if (Edges[i] < DT_MIN)
			log_warn(TAG,"edge %u: time %u too low",i,Edges[i]);
		else if (Edges[i] > DT_MAX)
			log_warn(TAG,"edge %u: time %u too high",i,Edges[i]);
	}
#endif

	if (m_errors > 0) {
		log_dbug(TAG,"got %u edges with unexpected timing",m_errors);
		return false;
	}
	if (m_bit != 40) {
		log_dbug(TAG,"got %u bits with %u edges instead of 40 bits",m_bit,m_edges);
		return false;
	}

	// verify checksum
	uint8_t cs = m_data[0]+m_data[1]+m_data[2]+m_data[3];
	if (m_data[4] != cs) {
		log_warn(TAG,"checksum error: %x + %x + %x + %x = %x, expected %x"
			, m_data[0], m_data[1], m_data[2], m_data[3]
			, cs
			, m_data[4]);
		return false;
	}

	// we made it
	m_error = false;
	double t = getTemperature();
	double h = getHumidity();
	if (m_temp)
		m_temp->set(t);
	if (m_humid)
		m_humid->set(h);
	if (log_module_enabled(TAG)) {
		char buf[8];
		float_to_str(buf,t);
		log_dbug(TAG,"temperature %s\u00b0C",buf);
		float_to_str(buf,h);
		log_dbug(TAG,"humidity %s%%",buf);
	}
	return true;
}


float DHT::getTemperature() const
{
	if (m_error)
		return NAN;
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
	if (m_error)
		return NAN;
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


