/*
 *  Copyright (C) 2021-2024, Thomas Maier-Komor
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

#ifdef CONFIG_I2C

#include "bmx.h"
#include "bmp388.h"
#include "bq25601d.h"
#include "i2cdrv.h"
#include "log.h"

#include <string.h>

#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
extern "C" esp_err_t i2c_hw_fsm_reset(i2c_port_t);
#endif

extern int bmx_scan(uint8_t);
extern int sgp30_scan(uint8_t);
extern int ccs811b_scan(uint8_t);
extern int ti_scan(uint8_t);
extern int apds9930_scan(uint8_t);
extern int pcf8574_scan(uint8_t);
extern int ssd1306_scan(uint8_t);
extern int bh1750_scan(uint8_t);

I2CDevice *I2CDevice::m_first = 0;

#define TAG MODULE_I2C
static uint8_t Ports = 0;
static SemaphoreHandle_t Mtx = 0;

#ifdef CONFIG_ESP_PHY_ENABLE_USB
static Charger *ChargerInstance;

Charger::Charger()
{
	ChargerInstance = this;
}

Charger *Charger::getInstance()
{ return ChargerInstance; }
#else
Charger::Charger()
{
}

Charger *Charger::getInstance()
{ return 0; }
#endif

int Charger::setImax(unsigned mA)
{ return -1; }

int Charger::getImax()
{ return -1; }


I2CDevice::I2CDevice(uint8_t bus, uint8_t addr, const char *name)
: m_next(m_first)
, m_bus(bus)
, m_addr(addr)
{
	if (name)
		strcpy(m_name,name);
	else
		abort();
	Lock lock(Mtx);
	bool had = hasInstance(name);
	m_first = this;
	if (had)
		updateNames(name);
}


void I2CDevice::addIntr(uint8_t intr)
{
	log_warn(TAG,"%s no interrupt support",m_name);
}


void I2CDevice::setName(const char *n)
{
	strncpy(m_name,n,sizeof(m_name)-1);
	m_name[sizeof(m_name)-1] = 0;
}


I2CDevice *I2CDevice ::getByAddr(uint8_t addr)
{
	I2CDevice *dev = m_first;
	while (dev) {
		if (dev->getAddr() == addr)
			return dev;
		dev = dev->m_next;
	}
	return 0;
}


bool I2CDevice::hasInstance(const char *d)
{
	size_t l = strlen(d);
	I2CDevice *s = m_first;
	while (s) {
		if (0 == strncmp(s->m_name,d,l))
			return true;
		s = s->m_next;
	}
	return false;
}


void I2CDevice::updateNames(const char *dev)
{
	// called from constructor: no virtual calls possible
	I2CDevice *s = m_first;
	while (s) {
		if (0 == strcmp(dev,s->m_name))
			s->updateName();
		s = s->m_next;
	}
}


void I2CDevice::updateName()
{
	// called from constructor: no virtual calls possible
	size_t off = strlen(m_name);
	int n = snprintf(m_name+off,sizeof(m_name)-off,"@%u:%x",m_bus,m_addr>>1);
	if (n+off >= sizeof(m_name)) {
		log_error(TAG,"name truncated: %s",m_name);
		m_name[sizeof(m_name)-1] = 0;
	}
}


void I2CDevice::attach(class EnvObject *)
{

}


int i2c_bus_valid(uint8_t port)
{
	return Ports & (1 << port);
}


int i2c_read(uint8_t port, uint8_t addr, uint8_t *d, uint8_t n)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	int r = -2;
	char p = 0;
	r = i2c_master_start(cmd);
	if (r) {
		p = 'S';
		goto done;
	}
	r = i2c_master_write_byte(cmd, addr|I2C_MASTER_READ, true);
	if (r) {
		p = 'a';
		goto done;
	}
	if (n == 1)
		r = i2c_master_read_byte(cmd, d, I2C_MASTER_LAST_NACK);
	else
		r = i2c_master_read(cmd, d, n, I2C_MASTER_LAST_NACK);
	if (r) {
		p = 'r';
		goto done;
	}
	r = i2c_master_stop(cmd);
	if (r) {
		p = 'p';
		goto done;
	}
	r = i2c_master_cmd_begin((i2c_port_t)port, cmd, 1000 / portTICK_PERIOD_MS);
	if (r) {
		p = 'x';
		goto done;
	}
done:
	log_hex(TAG,d,n,"i2c_read(%u,0x%x,*,%u)=%s %c",port,addr>>1,n,esp_err_to_name(r),p);
	i2c_cmd_link_delete(cmd);
	return r;
}


int i2c_w1rd(uint8_t port, uint8_t addr, uint8_t w, uint8_t *d, uint8_t n)
{
	int r = -2;
	char p = 0;
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	r = i2c_master_start(cmd);
	if (r) {
		p = 's';
		goto done;
	}
	r = i2c_master_write_byte(cmd, addr|I2C_MASTER_WRITE, true);
	if (r) {
		p = 'w';
		goto done;
	}
	r = i2c_master_write_byte(cmd, w, true);
	if (r) {
		p = 'n';
		goto done;
	}
	r = i2c_master_start(cmd);
	if (r) {
		p = 'S';
		goto done;
	}
	r = i2c_master_write_byte(cmd, addr|I2C_MASTER_READ, true);
	if (r) {
		p = 'W';
		goto done;
	}
	r = i2c_master_read(cmd, d, n, I2C_MASTER_LAST_NACK);
	if (r) {
		p = 'r';
		goto done;
	}
	r = i2c_master_stop(cmd);
	if (r) {
		p = 't';
		goto done;
	}
	r = i2c_master_cmd_begin((i2c_port_t)port, cmd, 1000 / portTICK_PERIOD_MS);
done:
	i2c_cmd_link_delete(cmd);
	log_hex(TAG,d,n,"i2c_w1rd(%u,0x%02x,0x%02x,...,%u)=%s %c",port,addr>>1,w,n,esp_err_to_name(r),p);
	return r;
}


int i2c_write0(uint8_t port, uint8_t addr)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	addr |= I2C_MASTER_WRITE;
	i2c_master_write_byte(cmd, addr, true);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write0(%u,0x%x<<1)=%s",port,addr>>1,esp_err_to_name(ret));
	return ret;
}


int i2c_write1(uint8_t port, uint8_t addr, uint8_t r)
{
	esp_err_t e;
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	e = i2c_master_start(cmd);
	assert(e == 0);
	uint8_t data[] = { (uint8_t)(addr|I2C_MASTER_WRITE), r };
	e = i2c_master_write(cmd, data, sizeof(data), true);
	assert(e == 0);
	e = i2c_master_stop(cmd);
	assert(e == 0);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write1(%u,0x%x<<1,0x%x)=%s",port,addr>>1,r,esp_err_to_name(ret));
	return ret;
}


int i2c_write2(uint8_t port, uint8_t addr, uint8_t r, uint8_t v)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	uint8_t data[] = { (uint8_t)(addr|I2C_MASTER_WRITE), r, v };
	i2c_master_write(cmd, data, sizeof(data), true);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write2(%u,0x%x<<1,0x%x,0x%x)=%s",port,addr>>1,r,v,esp_err_to_name(ret));
	return ret;
}


/*
int i2c_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *d, uint8_t n)
{
	uint8_t data[] = { (uint8_t)(addr|I2C_MASTER_WRITE), reg };
	return i2c_readback((i2c_port_t)port,data,sizeof(data),d,n);
}


int i2c_read2(uint8_t port, uint8_t addr, uint8_t reg0, uint8_t reg1, uint8_t *d, uint8_t n)
{
	uint8_t data[] = { (uint8_t)(addr|I2C_MASTER_WRITE), reg0, reg1 };
	return i2c_readback((i2c_port_t)port,data,sizeof(data),d,n);
}
*/


int i2c_write3(uint8_t port, uint8_t addr, uint8_t r0, uint8_t v0, uint8_t v1)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	uint8_t data[] =
		{ (uint8_t)(addr|I2C_MASTER_WRITE)
		, r0, v0, v1
		};
	i2c_master_write(cmd, data, sizeof(data), I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write4(%u,0x%x<<1,0x%x,0x%x,0x%x)=%d",port,addr>>1,r0,v0,v1,ret);
	return ret;
}


int i2c_write4(uint8_t port, uint8_t addr, uint8_t r0, uint8_t v0, uint8_t r1, uint8_t v1)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	uint8_t data[] =
		{ (uint8_t)(addr|I2C_MASTER_WRITE)
		, r0, v0
		, r1, v1
		};
	i2c_master_write(cmd, data, sizeof(data), I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write4(%u,0x%x,0x%x<<1,0x%x,0x%x,0x%x)=%d",port,addr>>1,r0,v0,r1,v1,ret);
	return ret;
}


int i2c_writen(uint8_t port, uint8_t addr, const uint8_t *d, unsigned n)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, addr | I2C_MASTER_WRITE, I2C_MASTER_NACK);
#ifdef CONFIG_IDF_TARGET_ESP8266
	i2c_master_write(cmd, (uint8_t*)d, n, I2C_MASTER_NACK);
#else
	i2c_master_write(cmd, d, n, I2C_MASTER_NACK);
#endif
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	if (log_module_enabled(TAG)) {
		log_hex(TAG,d,n,"i2c_writen(%u,0x%x<<1,0x%p,%u)=%d",port,addr>>1,d,n,ret);
	}
	return ret;
}


int i2c_writex(uint8_t port, const uint8_t *d, unsigned n)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
#ifdef CONFIG_IDF_TARGET_ESP8266
	i2c_master_write(cmd, (uint8_t*)d, n, I2C_MASTER_NACK);
#else
	i2c_master_write(cmd, d, n, I2C_MASTER_NACK);
#endif
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	if (log_module_enabled(TAG)) {
		log_hex(TAG,d,n,"i2c_writex(%u,0x%p,%u)=%d",port,d,n,ret);
	}
	return ret;
}


/*
int i2c_write1(uint8_t port, uint8_t d, bool stop)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, d, I2C_MASTER_NACK);
	if (stop)
		i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	if (log_module_enabled(TAG)) {
		log_dbug(TAG,"i2c_write(%u,0x%p,%u)=%d",port,d,n,ret);
		log_hex(TAG,d,n);
	}
	return ret;
}
*/


int i2c_write(uint8_t port, const uint8_t *d, unsigned n, uint8_t stop, uint8_t start)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	if (start)
		i2c_master_start(cmd);
#ifdef CONFIG_IDF_TARGET_ESP8266
	i2c_master_write(cmd, (uint8_t*)d, n, true);
#else
	i2c_master_write(cmd, d, n, true);
#endif
	if (stop)
		i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	log_hex(TAG,d,n,"i2c_write(%u,0x%p,%u,%d,%d)=%s",port,d,n,stop,start,esp_err_to_name(ret));
	return ret;
}


int i2c_write_nack(uint8_t port, uint8_t *d, unsigned n, uint8_t stop, uint8_t start)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	if (start)
		i2c_master_start(cmd);
	i2c_master_write(cmd, d, n, false);
	if (stop)
		i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	log_hex(TAG,d,n,"i2c_write_nack(%u,0x%p,%u,%d,%d)=%s",port,d,n,stop,start,esp_err_to_name(ret));
	return ret;
}


int i2c_init(uint8_t port, uint8_t sda, uint8_t scl, unsigned freq, uint8_t xpullup)
{
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	if (Ports & (1 << port)) {
		log_error(TAG,"duplicate port %d",port);
		return -1;
	}
	i2c_config_t conf;
	bzero(&conf,sizeof(conf));
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = (gpio_num_t) sda;
	conf.scl_io_num = (gpio_num_t) scl;
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
	conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;
#endif
	if (xpullup) {
		conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
		conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
	} else {
		conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
		conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	}
#ifdef ESP32
	conf.master.clk_speed = freq;
	// IRAM ISR placements seems to be unsupported on IDF v4.4.x
//	esp_err_t e = i2c_driver_install((i2c_port_t) port, conf.mode, 0, 0, ESP_INTR_FLAG_IRAM);
	esp_err_t e = i2c_driver_install((i2c_port_t) port, conf.mode, 0, 0, 0);
#else
	esp_err_t e = i2c_driver_install((i2c_port_t) port, conf.mode);
#endif
	if (e) {
		log_error(TAG,esp_err_to_name(e));
		return -1;
	}
	e = i2c_param_config((i2c_port_t) port, &conf);
	if (e) {
		log_error(TAG,"config: %s",esp_err_to_name(e));
		return -1;
	}
	// TODO: add config for timeout?
//	int timeout;
//	i2c_get_timeout((i2c_port_t) port, &timeout);
//	log_dbug(TAG,"default timeout %d",timeout);
//	i2c_set_timeout((i2c_port_t) port, 10*timeout);
//	TODO: add config for filter?
//	i2c_filter_enable((i2c_port_t)port,3);
//	TODO: add config for setup/hold timing?
//	int setup,hold;
//	i2c_get_start_timing((i2c_port_t) port, &setup, &hold);
//	log_dbug(TAG,"start timing: %u setup, %u hold",setup,hold);
//	i2c_set_start_timing((i2c_port_t) port, setup*4,hold*4);
	Ports |= (1 << port);
	// scan bus
	int n = 0;
	// For some reason the first bus access may result in a
	// bus-timeout. The BH1750 drivers deals with that situation, so
	// keep it the first in the scan!
#if defined CONFIG_IDF_TARGET_ESP32 || defined CONFIG_IDF_TARGET_ESP32S2 || defined CONFIG_IDF_TARGET_ESP32S3 || defined CONFIG_IDF_TARGET_ESP32C3
#if IDF_VERSION < 50
	esp_err_t r = i2c_hw_fsm_reset((i2c_port_t)port);
	assert(r == 0);
#endif
#endif
#ifdef CONFIG_CCS811B
	log_info(TAG,"search ccs811b");
	n += ccs811b_scan(port);
#endif
#ifdef CONFIG_BH1750
	// autoscan conflicts with TCA9555
	log_info(TAG,"search bh1750");
	n += bh1750_scan(port);
#endif
#ifdef CONFIG_BMX280
	log_info(TAG,"search bmx");
	n += bmx_scan(port);
#endif
#ifdef CONFIG_BMP388
	log_info(TAG,"search bmp388");
	n += bmp388_scan(port);
#endif
#ifdef CONFIG_SGP30
	log_info(TAG,"search sgp30");
	n += sgp30_scan(port);
#endif
#ifdef CONFIG_APDS9930
	log_info(TAG,"search apds9930");
	n += apds9930_scan(port);
#endif
#ifdef CONFIG_BQ25601D
	BQ25601D::scan(port);
#endif
	n += ti_scan(port);
	return n;
}

#endif // CONFIG_I2C
