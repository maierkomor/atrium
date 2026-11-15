/*
 *  Copyright (C) 2026, Thomas Maier-Komor
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

#ifdef CONFIG_SHT4X

#include "crc8.h"
#include "cyclic.h"
#include "i2cdrv.h"
#include "env.h"
#include "log.h"
#include "sht4x.h"
#include "terminal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#define TAG MODULE_SHT4X

#define SHT4x_ADDR	(0x44<<1)

#define CMD_MEAS_HP		0xfd
#define CMD_MEAS_MP		0xf6
#define CMD_MEAS_LP		0xe0
#define CMD_READ_SER		0x89
#define CMD_RESET		0x94
#define CMD_HEAT_200mW_1s	0x39
#define CMD_HEAT_200mW_100ms	0x32
#define CMD_HEAT_110mW_1s	0x2f
#define CMD_HEAT_110mW_100ms	0x24
#define CMD_HEAT_20mW_1s	0x1e
#define CMD_HEAT_20mW_100ms	0x15



SHT4x::SHT4x(uint8_t port, uint8_t addr, const char *n)
: I2CDevice(port,addr,n?n:drvName())
, m_temp("temperature","\u00b0C","%4.1f")
, m_humid("humidity","%","%4.1f")
{
}


void SHT4x::attach(EnvObject *r)
{
	log_dbug(TAG,"attach");
	r->add(&m_temp);
	r->add(&m_humid);
}


static int sht4x_get_serial(uint8_t bus, uint8_t addr, uint8_t *serial)
{
	if (0 != i2c_write1(bus,addr,CMD_READ_SER))
		return 1;
	vTaskDelay(1);	// minimum 1ms delay before read
	if (0 != i2c_read(bus,addr,serial,SIZE_SERIAL))
		return 1;
	uint8_t crc0 = crc8_0x31_2(serial[0],serial[1]);
	uint8_t crc1 = crc8_0x31_2(serial[3],serial[4]);
	if ((crc0 != serial[2]) || (crc1 != serial[5])) {
		log_warn(TAG,"sht4x at %d/%02x serial %02x %02x %02x %02x %02x %02x: CRC mismatch %02x?%02x, %02x?%02x"
				, bus, addr>>1
				, serial[0], serial[1], serial[2]
				, serial[3], serial[4], serial[5]
				, crc0, serial[2]
				, crc1, serial[5]
			);
		return 1;
	}
	log_info(TAG,"SHT4x at %d/%02x serial %02x %02x %02x %02x (CRC ok)"
			, bus, addr>>1
			, serial[0], serial[1]
			, serial[3], serial[4]
		);
	return 0;
}


SHT4x *SHT4x::create(uint8_t bus, uint8_t addr)
{
	if (i2c_write1(bus,addr,CMD_RESET)) {
		log_dbug(TAG,"SHT4x %d/%02x did not respond to reset request",bus,addr);
		return 0;
	}
	vTaskDelay(1);	// minimum 1ms delay before read
	uint8_t serial[SIZE_SERIAL];
	if (sht4x_get_serial(bus,addr,serial))
		return 0;
	SHT4x *d = new SHT4x(bus,addr);
	memcpy(d->m_serial,serial,sizeof(m_serial));
	d->init();
	return d;
}


const char *SHT4x::drvName() const
{
	return "sht4x";
}


unsigned SHT4x::cyclic(void *arg)
{
	SHT4x *drv = (SHT4x *)arg;
	return drv->cyclic();
}


unsigned SHT4x::cyclic()
{
	unsigned ret = 20;
	switch (m_state) {
	case st_meas_hp:
		send_cmd(CMD_MEAS_HP);
		ret = 10;
		m_state = st_read;
		break;
	case st_meas_mp:
		send_cmd(CMD_MEAS_MP);
		ret = 5;
		m_state = st_read;
		break;
	case st_meas_lp:
		send_cmd(CMD_MEAS_LP);
		ret = 2;
		m_state = st_read;
		break;
	case st_heat_1:
		send_cmd(CMD_HEAT_20mW_100ms);
		ret = 105;
		m_state = st_read;
		break;
	case st_heat_2:
		send_cmd(CMD_HEAT_20mW_1s);
		ret = 1010;
		m_state = st_read;
		break;
	case st_heat_3:
		send_cmd(CMD_HEAT_110mW_100ms);
		ret = 105;
		m_state = st_read;
		break;
	case st_heat_4:
		send_cmd(CMD_HEAT_110mW_1s);
		ret = 1010;
		m_state = st_read;
		break;
	case st_heat_5:
		send_cmd(CMD_HEAT_200mW_100ms);
		ret = 105;
		m_state = st_read;
		break;
	case st_heat_6:
		send_cmd(CMD_HEAT_200mW_1s);
		ret = 1010;
		m_state = st_read;
		break;
	case st_read:
		read_values();
		m_state = st_idle;
		break;
	case st_reset:
		send_cmd(CMD_RESET);
		m_state = st_idle;
		break;
	case st_idle:
		switch (m_mode) {
		case md_off:
			break;
		case md_lp:
			m_state = st_meas_lp;
			break;
		case md_mp:
			m_state = st_meas_mp;
			break;
		case md_hp:
			m_state = st_meas_hp;
			break;
		}
		break;
	default:
		abort();
	}
	return ret;
}


void SHT4x::reset()
{
	if (esp_err_t e = i2c_write1(m_bus,m_addr,CMD_RESET)) {
		log_warn(TAG,"reset I2C error: %s",esp_err_to_name(e));
	} else {
		log_info(TAG,"reset triggered");
	}
}


int SHT4x::send_cmd(uint8_t cmd)
{
	return i2c_write1(m_bus,m_addr,cmd);
}


void SHT4x::read_values()
{
	uint8_t data[6];
	float temp = NAN, humid = NAN;
	if (esp_err_t e = i2c_read(m_bus,m_addr,data,sizeof(data))) {
		log_warn(TAG,"I2C error on device %u/%02x: %s",m_bus,m_addr,esp_err_to_name(e));
	} else {
		log_hex(TAG,data,sizeof(data),"measurement data");
		if (data[2] == crc8_0x31(data,2)) {
			uint16_t t = (data[0] << 8) | data[1];
			temp = -45 + 175.0 * (float)t / 65535.0;
		} else {
			log_warn(TAG,"CRC error on temperature of device %u/%02x",m_bus,m_addr);
		}
		if (data[5] == crc8_0x31(data+3,2)) {
			uint16_t h = (data[3] << 8) | data[4];
			humid = -6 + 125.0 * (float)h / 65535.0;
		} else {
			log_warn(TAG,"CRC error on humidity of device %u/%02x",m_bus,m_addr);
		}
	}
	m_temp.set(temp);
	m_humid.set(humid);
}


#ifdef CONFIG_I2C_XCMD
const char *SHT4x::exeCmd(Terminal &term, int argc, const char **args)
{
	if (argc == 0) {
		term.printf("possible commands are:\n"
				"reset: reset device\n"
				"id   : print serial of device\n"
				"heat [1..6]: heat device\n"
				"mode [off|low|medium|high]\n");
	} else if (0 == strcmp("reset",args[0])) {
		reset();
	} else if (0 == strcmp("id",args[0])) {
		term.printf("serial %02x %02x %02x %02x\n", m_serial[0], m_serial[1], m_serial[3], m_serial[4]);
	} else if (0 == strcmp("mode",args[0])) {
		if (argc < 2)
			return "Invalid number of arguments.";
		if ('l' == args[1][0])
			m_mode = md_lp;
		else if ('m' == args[1][0])
			m_mode = md_mp;
		else if ('h' == args[1][0])
			m_mode = md_hp;
		else if ('o' == args[1][0])
			m_mode = md_off;
		else
			return "Invalid argument #2.";
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}
#endif

int SHT4x::init()
{
	int r = cyclic_add_task("sht4x",SHT4x::cyclic,this,220);
	return r;
}


unsigned sht4x_scan(uint8_t bus)
{
	unsigned num = 0;
	log_info(TAG,"search devices");
	for (uint8_t addr = 0x44; addr <= 0x46; ++addr) {
		if (0 == I2CDevice::getByAddr(bus,addr<<1))
			num += SHT4x::create(bus,addr<<1) != 0;
	}
	return num;
}

#endif
