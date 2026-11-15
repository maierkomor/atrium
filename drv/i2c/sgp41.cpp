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

#ifdef CONFIG_SGP41

#include "crc8.h"
#include "cyclic.h"
#include "i2cdrv.h"
#include "env.h"
#include "log.h"
#include "sgp41.h"
#include "terminal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>


#define CMD_EXE_COND	0x26, 0x12
#define CMD_MEAS_RAW	0x26, 0x19
#define CMD_BIST	0x28, 0x0e
#define CMD_HEATER_OFF	0x36, 0x15
#define CMD_GET_SERIAL	0x36, 0x82
#define CMD_RESET	0x06
#define I2C_ALL		0x0

#define SERIAL_LENGTH	9


#define TAG MODULE_SGP


static const char *States[] = {
	"idle",
	"trigger-condition",
	"wait-condition",
	"trigger-measure",
	"wait-measure",
	"trigger-bist",
	"wait-bist",
	"trigger-serial",
	"wait-serial",
	"heater-off",
};


static int sgp41_get_serial(uint8_t bus, uint8_t *serial)
{
	// i2c_write2 is needed here, due to an IDF bug.
	// A sequence of address plus 2 bytes is not handled correctly.
	if (0 != i2c_write2(bus,SGP41_ADDR,CMD_GET_SERIAL)) {
		log_dbug(TAG,"no SGP41 on bus %u",bus);
		return 0;
	}
	vTaskDelay(2);	// minimum 1ms delay before read
	if (esp_err_t e = i2c_read(bus,SGP41_ADDR,serial,SERIAL_LENGTH)) {
		log_dbug(TAG,"device serial read on bus %u: %s",bus,esp_err_to_name(e));
		return 0;
	}
	uint8_t crc0 = crc8_0x31(serial+0,2);
	uint8_t crc1 = crc8_0x31(serial+3,2);
	uint8_t crc2 = crc8_0x31(serial+6,2);
	if ((crc0 != serial[2]) || (crc1 != serial[5]) || (crc2 != serial[8])) {
		log_hex(TAG,serial,sizeof(serial),"device on bus %u: version CRC mismatch",bus);
		return 0;
	}
	return 1;
}


SGP41::SGP41(uint8_t port)
: I2CDevice(port,SGP41_ADDR,drvName())
, m_voc("VOC","ppb","%4.0f")
, m_nox("NOX","ppm","%4.0f")
{
	bzero(m_serial,sizeof(m_serial));
}


SGP41::SGP41(uint8_t port, uint8_t addr, const char *n)
: I2CDevice(port,addr,n)
, m_voc("VOC","ppb","%4.0f")
, m_nox("NOX","ppm","%4.0f")
{
	bzero(m_serial,sizeof(m_serial));
}


void SGP41::attach(EnvObject *r)
{
	log_dbug(TAG,"attach");
	r->add(&m_voc);
	r->add(&m_nox);
}


SGP41 *SGP41::create(uint8_t bus)
{
	SGP41 *d = 0;
	if (0 == I2CDevice::getByAddr(bus,SGP41_ADDR)) {
		uint8_t serial[SERIAL_LENGTH];
		log_info(TAG,"searching for SGP4x");
		if (sgp41_get_serial(bus,serial)) {
			d = new SGP41(bus);
			d->init();
		}
	}
	return d;
}


const char *SGP41::drvName() const
{
	return "sgp41";
}


unsigned SGP41::cyclic(void *arg)
{
	SGP41 *drv = (SGP41 *)arg;
	return drv->cyclic();
}


unsigned SGP41::cyclic()
{
	unsigned del = 20;
	esp_err_t e = 0;
	switch (m_state) {
	case st_idle:
		m_state = st_tr_meas_raw;
		return 50;
	case st_tr_exe_cond:
		e = start_conditioning();
		if (0 == e) {
			m_state = st_wt_exe_cond;
			return 50;
		}
		break;
	case st_wt_exe_cond:
		e = done_conditioning();
		if (0 == e) {
			m_state = st_idle;
			return 100;
		}
		break;
	case st_tr_reset:
		reset();
		m_state = st_tr_bist;
		return 10;
	case st_tr_bist:
		e = start_bist();
		if (0 == e) {
			m_state = st_wt_bist;
			return 325;
		}
		break;
	case st_wt_bist:
		e = read_bist();
		if (0 == e) {
			m_state = st_tr_exe_cond;
			return 200;
		}
		break;
	case st_tr_get_serial:
		e = get_serial();
		if (0 == e) {
			m_state = st_wt_get_serial;
			return 2;
		}
		break;
	case st_wt_get_serial:
		e = read_serial();
		m_state = st_idle;
		break;
	case st_tr_meas_raw:
		e = trigger_measure();
		if (0 == e) {
			m_state = st_wt_meas_raw;
			return 55;
		}
		break;
	case st_wt_meas_raw:
		e = read_measure();
		if (0 == e) {
			m_state = st_idle;
			return 100;
		}
		break;

	case st_error:
		m_state = st_tr_bist;
		return 100;
	default:
		abort();
	}
	if (e) {
		m_voc.set(NAN);
		m_nox.set(NAN);
		m_state = st_error;
		return 2000;
	}
	return del;
}


#ifdef CONFIG_I2C_XCMD
const char *SGP41::exeCmd(Terminal &term, int argc, const char **args)
{
	if (argc == 0) {
		term.printf("state: %s\n",States[m_state]);
	} else if (0 == strcmp("id",args[0])) {
		term.printf("serial id ");
		term.print_hex(m_serial,sizeof(m_serial));
		term.println();
	} else if (0 == strcmp("-h",args[0])) {
		term.println("valid commands are: reset, stop, bist, cond, measuere");
	} else if (0 == strcmp("reset",args[0])) {
		m_state = st_tr_reset;
	} else if (0 == strcmp("stop",args[0])) {
		m_state = st_idle;
	} else if (0 == strcmp("bist",args[0])) {
		m_state = st_tr_exe_cond;
	} else if (0 == strcmp("cond",args[0])) {
		m_state = st_tr_exe_cond;
	} else if (0 == strcmp("measuere",args[0])) {
		m_state = st_tr_meas_raw;
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}
#endif


int SGP41::init()
{
	int r = cyclic_add_task("sgp41",SGP41::cyclic,this,220);
	return r;
}


int SGP41::reset()
{
	return reset(m_bus);
}


int SGP41::reset(uint8_t bus)
{
	// This triggers a reset of all devices on the bus that support.
	// This is the general call reset command.
	esp_err_t e = i2c_write1(bus,I2C_ALL,CMD_RESET);
	log_info(TAG,"device reset requested");
	return e;
}


int SGP41::trigger_measure()
{
	uint8_t meas[] = {
		SGP41_ADDR, CMD_MEAS_RAW,
		0x80, 0x00, 0xa2,
		0x66, 0x66, 0x93 };
	if (m_humid) {
		float h = m_humid->get();
		if (!isnan(h)) {
			uint16_t humid = (uint16_t)rintf(h*65535.0/100);
			log_dbug(TAG,"measure humidity compensation %u",humid);
			meas[3] = humid >> 8;
			meas[4] = humid & 0xff;
			meas[5] = crc8_0x31_2(humid>>8,humid&0xff);
		}
	}
	if (m_temp) {
		float t = m_temp->get();
		if (!isnan(t)) {
			uint16_t temp = (uint16_t)rintf((t + 45.0)*65535.0/175.0);
			log_dbug(TAG,"measure temperature compensation %u",temp);
			meas[6] = temp >> 8;
			meas[7] = temp & 0xff;
			meas[8] = crc8_0x31_2(temp>>8,temp&0xff);
		}
	}
	esp_err_t e = i2c_write(m_bus,meas,sizeof(meas));
	if (e)
		log_warn(TAG,"start measure: %s",esp_err_to_name(e));
	else
		log_dbug(TAG,"started measurement");
	return e;
}


int SGP41::read_measure()
{
	uint8_t data[6];
	esp_err_t e = i2c_read(m_bus,SGP41_ADDR,data,sizeof(data));
	if (0 == e) {
		log_hex(TAG,data,sizeof(data),"measure data");
		float voc,nox;
		if (crc8_0x31_2(data[0],data[1]) == data[2]) {
			voc = (float)((data[0] << 8) | data[1]);
		} else {
			log_warn(TAG,"CRC error in VOC value");
			voc = NAN;
		}
		if (crc8_0x31_2(data[3],data[4]) == data[5]) {
			nox = (float)((data[3] << 8) | data[4]);
		} else {
			log_warn(TAG,"CRC error in NOX value");
			nox = NAN;
		}
		m_voc.set(voc);
		m_nox.set(nox);
	} else {
		log_warn(TAG,"read measure data failed: %s",esp_err_to_name(e));
	}
	return e;
}


int SGP41::start_conditioning()
{
	uint8_t cond[] = { SGP41_ADDR, 0x26, 0x12, 0x80, 0x00, 0xa2, 0x66, 0x66, 0x93 };
	esp_err_t e = i2c_write(m_bus,cond,sizeof(cond));
	if (e)
		log_warn(TAG,"start conditioning failed");
	else
		log_dbug(TAG,"start conditionioning");
	return e;
}


int SGP41::done_conditioning()
{
	uint8_t cond[3];
	esp_err_t e = i2c_read(m_bus,SGP41_ADDR,cond,sizeof(cond));
	if (e)
		log_warn(TAG,"conditioning read failed");
	else
		log_dbug(TAG,"conditionioning finished");
	uint8_t crc = crc8_0x31(cond,2);
	if (crc == cond[2]) {
		m_voc.set((cond[0]<<8)|cond[1]);
	} else {
		log_warn(TAG,"VOC CRC error");
		m_voc.set(NAN);
	}
	return e;
}


int SGP41::start_bist()
{
	uint8_t bist[] = { SGP41_ADDR, 0x28, 0x0e };
	esp_err_t e = i2c_write(m_bus,bist,sizeof(bist));
	if (e)
		log_warn(TAG,"start BIST failed");
	else
		log_info(TAG,"start BIST");
	return e;

}


int SGP41::read_bist()
{
	if (m_temp == 0) {
		if (EnvObject *r = m_voc.getParent()) {
			if (EnvElement *e = r->find("temperature")) {
				if (EnvNumber *n = e->toNumber()) {
					log_dbug(TAG,"found temperature sensor");
					m_temp = n;
				}
			}
		}
	}
	if (m_humid == 0) {
		if (EnvObject *r = m_voc.getParent()) {
			if (EnvElement *e = r->find("humidity")) {
				if (EnvNumber *n = e->toNumber()) {
					log_dbug(TAG,"found humidity sensor");
					m_humid = n;
				}
			}
		}
	}
	uint8_t result[3];
	if (esp_err_t e = i2c_read(m_bus,m_addr,result,sizeof(result))) {
		return e;
	} else if (result[2] != crc8_0x31(result,2)) {
		log_hex(TAG,result,sizeof(result),"BIST result");
		log_warn(TAG,"CRC error in BIST result");
		m_state = st_error;
	} else {
		if (result[1] & 1) {
			log_warn(TAG,"VOC BIST failure");
		}
		if (result[1] & 2) {
			log_warn(TAG,"NOx BIST failure");
		}
	}
	return 0;
}


int SGP41::get_serial()
{
	uint8_t cmd[] = { SGP41_ADDR, 0x36, 0x82 };	// get serial ID
	esp_err_t e = i2c_write(m_bus,cmd,sizeof(cmd));
	if (e) {
		log_warn(TAG,"get serial id: %s",esp_err_to_name(e));
	}
	return e;
}


int SGP41::read_serial()
{
	uint8_t serial[6];
	esp_err_t e = i2c_read(m_bus,SGP41_ADDR,serial,sizeof(serial));
	if (e) {
		log_warn(TAG,"read serial id failed: %s",esp_err_to_name(e));
	} else {
		memcpy(m_serial,serial,sizeof(serial));
		log_hex(TAG,m_serial,sizeof(m_serial),"serial id ");
		if ((crc8_0x31(m_serial+0,2) != m_serial[2]) || (crc8_0x31(m_serial+3,2) != m_serial[5])  || (crc8_0x31(m_serial+6,2) != m_serial[8]))
			log_warn(TAG,"CRC error in serial number");
	}
	return e;
}


void SGP41::getParameters(uint8_t *p)
{
	if (m_temp == 0) {
		if (EnvObject *r = m_voc.getParent()) {
			if (EnvElement *e = r->find("temperature")) {
				if (EnvNumber *n = e->toNumber()) {
					size_t l = n->getPath(0,0);
					char path[l+1];
					n->getPath(path,'/');
					log_dbug(TAG,"found temperature sensor %s",path);
					m_temp = n;
				}
			}
		}
	}
	if (m_humid == 0) {
		if (EnvObject *r = m_voc.getParent()) {
			if (EnvElement *e = r->find("humidity")) {
				if (EnvNumber *n = e->toNumber()) {
					size_t l = n->getPath(0,0);
					char path[l+1];
					n->getPath(path,'/');
					log_dbug(TAG,"found humidity sensor %s",path);
					m_humid = n;
				}
			}
		}
	}
			   //
	// default parameter without compensation 
	uint8_t dp[] = {0x80,0x00,0xa2,0x66,0x66,0x93};
	memcpy(p,dp,sizeof(dp));
	float humid = m_humid ? m_humid->get() : NAN;
	if (!isnan(humid)) {
		if ((0 <= humid) && (humid <= 100)) {
			uint16_t h = (uint16_t)rintf((humid * 65535) / 100);
			p[0] = h>>8;
			p[1] = h;
			p[2] = crc8_0x31(p,2);
		}
	}
	float temp = m_temp ? m_temp->get() : NAN;
	if (!isnan(temp)) {
		if ((-45 <= temp) && (temp <= 130)) {
			uint16_t t = (uint16_t)rintf(((temp+45) * 65535) / 175);
			p[0] = t>>8;
			p[1] = t;
			p[2] = crc8_0x31(p+3,2);
		}
	}
}


int SGP41::sample()
{
	uint8_t cmd[9] = { SGP41_ADDR, CMD_MEAS_RAW };
	getParameters(cmd+3);
	if (esp_err_t e = i2c_write(m_bus, cmd, sizeof(cmd))) {
		log_warn(TAG,"sample req failed: %s",esp_err_to_name(e));
		return e;
	}
	log_dbug(TAG,"sample");
	return 0;
}


int SGP41::read()
{
	uint8_t data[6];
	double voc = NAN, nox = NAN;
	int r = 0;
	if (esp_err_t e = i2c_read(m_bus,SGP41_ADDR,data,sizeof(data))) {
		log_warn(TAG,"read req failed: %s",esp_err_to_name(e));
		r = 1;
	} else {
		// log_hex(TAG,data,sizeof(data),"airq");
		uint8_t vcrc = crc8_0x31(data,2);
		if (vcrc == data[2]) {
			uint16_t voci = data[0] << 8 | data[1];
			log_dbug(TAG,"co2=%u",voci);
			voc = voci;
		} else {
			log_warn(TAG,"co2eq CRC error: got %x, expected %x",vcrc,data[2]);
			m_state = st_error;
		}
		uint8_t ncrc = crc8_0x31(data+3,2);
		if (ncrc == data[5]) {
			uint16_t noxi = data[3] << 8 | data[4];
			log_dbug(TAG,"nox=%u",noxi);
			nox = noxi;
		} else {
			log_warn(TAG,"tcov CRC error: get %x, expected %x",ncrc,data[5]);
		}
	}
	m_voc.set(voc);
	m_nox.set(nox);
	return r;
}


unsigned sgp41_scan(uint8_t bus)
{
	return SGP41::create(bus) != 0;
}

#endif
