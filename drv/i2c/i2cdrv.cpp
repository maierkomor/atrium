/*
 *  Copyright (C) 2021, Thomas Maier-Komor
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

#include "bmx.h"
#include "i2cdrv.h"
#include "log.h"

#include <string.h>

#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>


extern int bmx_scan(uint8_t);

I2CSensor *I2CSensor::m_first = 0;

static const char TAG[] = "i2c";
static uint8_t Ports = 0;
static SemaphoreHandle_t Mtx = 0;


I2CSensor::I2CSensor(uint8_t bus, uint8_t addr, const char *name)
: m_bus(bus), m_addr(addr)
{
	log_info(TAG,"attaching %s",name);
	xSemaphoreTake(Mtx,portMAX_DELAY);
	int n = snprintf(m_name,sizeof(m_name),"%s@%u,%x",name,bus,addr>>1);
	if (n >= sizeof(m_name))
		log_error(TAG,"name truncated: %s",m_name);
	m_next = m_first;
	m_first = this;
	xSemaphoreGive(Mtx);
}


I2CSensor::~I2CSensor()
{
}


void I2CSensor::setName(const char *n)
{
	strncpy(m_name,n,sizeof(m_name)-1);
	m_name[sizeof(m_name)-1] = 0;
}


int I2CSensor::init()
{
	return 0;
}


void I2CSensor::sample(void *x)
{
	I2CSensor *s = (I2CSensor *)x;
	s->sample();
}


int I2CSensor::sample()
{
	return 0;
}

void I2CSensor::attach(class JsonObject *)
{

}


int i2c_bus_valid(uint8_t port)
{
	return Ports & (1 << port);
}


int i2c_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *d, uint8_t n)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	if (int r = i2c_master_start(cmd)) {
		log_dbug(TAG,"read/0: %d",r);
		return r;
	}
	uint8_t data[] = { (uint8_t)(addr|I2C_MASTER_WRITE), reg };
	if (int r = i2c_master_write(cmd, data, sizeof(data), I2C_MASTER_NACK)) {
		log_dbug(TAG,"read/1: %d",r);
		return r;
	}
	if (int r = i2c_master_start(cmd)) {
		log_dbug(TAG,"read/2: %d",r);
		return r;
	}
	if (int r = i2c_master_write_byte(cmd, addr | I2C_MASTER_READ, I2C_MASTER_NACK)) {
		log_dbug(TAG,"read/3: %d",r);
		return r;
	}
	if (int r = i2c_master_read(cmd, d, n, I2C_MASTER_LAST_NACK)) {
		log_dbug(TAG,"read/4: %d",r);
		return r;
	}
	if (int r = i2c_master_stop(cmd)) {
		log_dbug(TAG,"read/5: %d",r);
		return r;
	}
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if (log_module_enabled(TAG)) {
		log_dbug(TAG,"i2c_read(%u,0x%x,0x%x,%p,%u)=%d",port,addr,reg,d,n,ret);
		log_hex(TAG,d,n);
	}
	return ret;
}


int i2c_write2(uint8_t port, uint8_t addr, uint8_t r, uint8_t v)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	uint8_t data[] = { (uint8_t)(addr|I2C_MASTER_WRITE), r, v };
	i2c_master_write(cmd, data, sizeof(data), I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write2(%u,0x%x,0x%x,0x%x)=%d",port,addr,r,v,ret);
	return ret;
}


int i2c_write4(uint8_t port, uint8_t addr, uint8_t r0, uint8_t v0, uint8_t r1, uint8_t v1)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	uint8_t data[] =
		{ (uint8_t)(addr|I2C_MASTER_WRITE), I2C_MASTER_NACK
		, r0, v0
		, r1, v1
		};
	i2c_master_write(cmd, data, sizeof(data), I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write4(%u,0x%x,0x%x,0x%x,0x%x,0x%x)=%d",port,addr,r0,v0,r1,v1,ret);
	return ret;
}


int i2c_writen(uint8_t port, uint8_t addr, uint8_t *d, unsigned n)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, addr | I2C_MASTER_WRITE, I2C_MASTER_NACK);
	i2c_master_write(cmd, d, n, I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if (log_module_enabled(TAG)) {
		log_dbug(TAG,"i2c_writen(%u,0x%x,0x%p,%u)=%d",port,addr,d,n,ret);
		log_hex(TAG,d,n);
	}
	return ret;
}


int i2c_write(uint8_t port, uint8_t *d, unsigned n)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write(cmd, d, n, I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if (log_module_enabled(TAG)) {
		log_dbug(TAG,"i2c_write(%u,0x%p,%u)=%d",port,d,n,ret);
		log_hex(TAG,d,n);
	}
	return ret;
}


int i2c_init(uint8_t port, uint8_t sda, uint8_t scl, unsigned freq)
{
	if (Mtx == 0)
		Mtx = xSemaphoreCreateMutex();
	if (Ports & (1 << port)) {
		log_error(TAG,"duplicate port %d",port);
		return -11;
	}
	i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = (gpio_num_t) sda;
	conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	conf.scl_io_num = (gpio_num_t) scl;
	conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
#ifdef CONFIG_IDF_TARGET_ESP32
	conf.master.clk_speed = freq;
	esp_err_t e = i2c_driver_install((i2c_port_t) port, conf.mode, 0, 0, 0);
#else
	esp_err_t e = i2c_driver_install((i2c_port_t) port, conf.mode);
#endif
	if (e) {
		log_error(TAG,"i2c driver: %s",esp_err_to_name(e));
		return -1;
	}
	e = i2c_param_config((i2c_port_t) port, &conf);
	if (e) {
		log_error(TAG,"i2c param: %s",esp_err_to_name(e));
		return -1;
	}
	log_dbug(TAG,"i2c port %au: sda=%u,scl=%u",port,sda,scl);
	Ports |= (1 << port);
	// scan bus
	return bmx_scan(port);
}
