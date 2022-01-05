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

#include <sdkconfig.h>

#ifdef CONFIG_I2C

#include "bmx.h"
#include "i2cdrv.h"
#include "log.h"

#include <string.h>

#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>


extern int bmx_scan(uint8_t);
extern int sgp30_scan(uint8_t);
extern int ht16k33_scan(uint8_t);
extern int ccs811b_scan(uint8_t);
extern int ti_scan(uint8_t);
extern int apds9930_scan(uint8_t);
extern int pcf8574_scan(uint8_t);
extern int  ssd1306_scan(uint8_t);

I2CDevice *I2CDevice::m_first = 0;

#define TAG MODULE_I2C
static uint8_t Ports = 0;
static SemaphoreHandle_t Mtx = 0;


I2CDevice::I2CDevice(uint8_t bus, uint8_t addr, const char *name)
: m_bus(bus), m_addr(addr)
{
	strcpy(m_name,name);
	bool x = hasInstance(name);
	log_info(TAG,"%s on bus %d, id 0x%x",name,bus,addr);
	xSemaphoreTake(Mtx,portMAX_DELAY);
	m_next = m_first;
	m_first = this;
	if (x)
		updateNames(name);
	xSemaphoreGive(Mtx);
}


void I2CDevice::setName(const char *n)
{
	strncpy(m_name,n,sizeof(m_name)-1);
	m_name[sizeof(m_name)-1] = 0;
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
	r = i2c_master_write_byte(cmd, addr|I2C_MASTER_READ, I2C_MASTER_NACK);
	if (r) {
		p = 'a';
		goto done;
	}
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
	r = i2c_master_cmd_begin((i2c_port_t)port, cmd, 1000 / portTICK_RATE_MS);
	if (r) {
		p = 'x';
		goto done;
	}
done:
	log_hex(TAG,d,n,"i2c_read(%u,0x%x,*,%u)=%d %c",port,addr>>1,n,r,p);
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
	r = i2c_master_write_byte(cmd, addr|I2C_MASTER_WRITE, I2C_MASTER_NACK);
	if (r) {
		p = 'w';
		goto done;
	}
	r = i2c_master_write_byte(cmd, w, I2C_MASTER_NACK);
	if (r) {
		p = 'n';
		goto done;
	}
	r = i2c_master_start(cmd);
	if (r) {
		p = 'S';
		goto done;
	}
	r = i2c_master_write_byte(cmd, addr|I2C_MASTER_READ, I2C_MASTER_NACK);
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
	r = i2c_master_cmd_begin((i2c_port_t)port, cmd, 1000 / portTICK_RATE_MS);
done:
	i2c_cmd_link_delete(cmd);
	log_hex(TAG,d,n,"i2c_w1rd(%u,%02x,%02x,...,%u)=%d %c",port,addr>>1,w,n,r,p);
	return r;
}


int i2c_write1(uint8_t port, uint8_t addr, uint8_t r)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	uint8_t data[] = { (uint8_t)(addr|I2C_MASTER_WRITE), r };
	i2c_master_write(cmd, data, sizeof(data), I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write1(%u,0x%x,0x%x)=%d",port,addr>>1,r,ret);
	return ret;
}


int i2c_write2(uint8_t port, uint8_t addr, uint8_t r, uint8_t v)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	uint8_t data[] = { (uint8_t)(addr|I2C_MASTER_WRITE), r, v };
	i2c_master_write(cmd, data, sizeof(data), I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	log_dbug(TAG,"i2c_write2(%u,0x%x,0x%x,0x%x)=%d",port,addr>>1,r,v,ret);
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

int i2c_write1(uint8_t port, uint8_t d, bool stop)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, d, I2C_MASTER_NACK);
	if (stop)
		i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if (log_module_enabled(TAG)) {
		log_dbug(TAG,"i2c_write(%u,0x%p,%u)=%d",port,d,n,ret);
		log_hex(TAG,d,n);
	}
	return ret;
}
*/


int i2c_write(uint8_t port, uint8_t *d, unsigned n, uint8_t stop, uint8_t start)
{
	Lock lock(Mtx);
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	if (start)
		i2c_master_start(cmd);
	i2c_master_write(cmd, d, n, I2C_MASTER_NACK);
	if (stop)
		i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin((i2c_port_t) port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	log_hex(TAG,d,n,"i2c_write(%u,0x%p,%u)=%d",port,d,n,ret);
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
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = (gpio_num_t) sda;
	conf.scl_io_num = (gpio_num_t) scl;
	if (xpullup) {
		conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
		conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
	} else {
		conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
		conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	}
#ifdef CONFIG_IDF_TARGET_ESP32
	conf.master.clk_speed = freq;
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
	log_dbug(TAG,"i2c %u: sda=%u,scl=%u %s pull-up",port,sda,scl,xpullup?"extern":"intern");
	Ports |= (1 << port);
	// scan bus
	int n = 0;
#ifdef CONFIG_BMX280
	n += bmx_scan(port);
#endif
#ifdef CONFIG_SGP30
	n += sgp30_scan(port);
#endif
#ifdef CONFIG_CCS811B
	n += ccs811b_scan(port);
#endif
#ifdef CONFIG_APDS9930
	n += apds9930_scan(port);
#endif
	n += ti_scan(port);
#ifdef CONFIG_PCF8574
	n += pcf8574_scan(port);
#endif
#ifdef CONFIG_SSD1306
	n += ssd1306_scan(port);
#endif
#ifdef CONFIG_HT16K33
	n += ht16k33_scan(port);
#endif
	return n;
}

#endif // CONFIG_I2C
