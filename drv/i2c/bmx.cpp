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

// This drivers operates the BME280 in I2C mode.
// Connect CSB to 3.3V to enable I2C mode.
// Connect SDO to either GND or VCC to select the base address.

//#define USE_DOUBLE

#include <esp_err.h>
#include <strings.h>

#include "actions.h"
#include "bmx.h"
#include "cyclic.h"
#include "event.h"
#include "i2cdrv.h"
#include "log.h"
#include "stream.h"
#include "terminal.h"
#include "env.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define BME_ADDR_MIN		0xec
#define BME_ADDR_MAX		0xee

#define BMP280_ID		0x58
#define BME280_ID		0x60

#define BME_REG_ID		0xd0
#define BME280_REG_RESET	0xe0
#define BME280_REG_CALIB	0xe1
#define BME280_REG_HLSB		0xfe
#define BME280_REG_HMSB		0xfd
#define BME280_REG_HXLSB	0xfc
#define BME280_REG_TLSB		0xfb
#define BME280_REG_TMSB		0xfa
#define BME280_REG_PXLSB	0xf9
#define BME280_REG_PLSB		0xf8
#define BME280_REG_PMSB		0xf7
#define BME280_REG_CONFIG	0xf5
#define BME280_REG_STATUS	0xf3
#define BME280_REG_CTRLMEAS	0xf4
#define BME280_REG_CTRLHUM	0xf2

#define BME280_CALIB_DATA	0x88
#define BMX280_REG_BASE		0xf7

#define BME280_I2C_ADDR 0xec
#define BME280_READ 1
#define BME280_WRITE 0

#define BME280_START() bme_write1(0xf4,0xb5)
#define BME280_RESET() bme_write1(0xe0,0xb6)

#define TAG MODULE_BMX


#ifdef CONFIG_BMX280

BMP280::BMP280(uint8_t port, uint8_t addr, const char *n)
: I2CDevice(port,addr,n ? n : drvName())
{
	m_temp = new EnvNumber("temperature","\u00b0C","%4.1f");
	m_press = new EnvNumber("pressure","hPa","%4.1f");
}


BME280::BME280(uint8_t port, uint8_t addr)
: BMP280(port,addr,drvName())
{
	m_humid = new EnvNumber("humidity","%","%4.1f");
}


void BMP280::trigger(void *arg)
{
	BMP280 *dev = (BMP280 *)arg;
	if (dev->m_state == st_idle)
		dev->m_state = st_sample;
}


unsigned BMP280::cyclic(void *arg)
{
	BMP280 *drv = (BMP280 *) arg;
	switch (drv->m_state) {
	case st_idle:
		return 20;
	case st_sample:
		if (drv->sample())
			break;
		drv->m_state = st_measure;
		return 10;
	case st_measure:
		if (drv->status())
			break;
		return 5;
	case st_read:
		if (drv->read())
			break;
		drv->m_state = st_idle;
		return 50;
	default:
		abort();
	}
	drv->handle_error();
	return 1000;
}


void BMP280::attach(EnvObject *root)
{
	root->add(m_temp);
	root->add(m_press);
	cyclic_add_task(m_name,BMP280::cyclic,this,0);
	action_add(concat(m_name,"!sample"),trigger,(void*)this,"BMP280 sample data");
}


void BME280::attach(EnvObject *root)
{
	BMP280::attach(root);
	root->add(m_humid);
}


#ifdef CONFIG_I2C_XCMD
const char *BMP280::exeCmd(Terminal &term, int argc, const char **args)
{
	if ((argc == 0) || ((argc == 1) && (0 == strcmp(args[0],"-h")))) {
		term.println(
			"set oversampling, valid values 0,1,2,4,8,16\n"
			"iir <m>: set IIR filter\n"
			"tos <m>: for temperature\n"
			"pos <m>: for pressure"
			);
		return 0;
	}
	if (argc == 1) {
		int8_t s;
		if (0 == strcmp(args[0],"tos")) {
			s = m_sampmod>>5;
		} else if (0 == strcmp(args[0],"pos")) {
			s = m_sampmod>>2;
			s &= 7;
		} else {
			return "Invalid argument #1.";
		}
		term.printf("x%u\n",s != 0 ? 1<<--s : 0);
		return 0;
	}
	if (argc != 2)
		return "Invalid number of arguments.";
	char *e;
	long v = strtol(args[1],&e,0);
	if (*e)
		return "Invalid argument #2.";
	uint8_t x;
	if ((v >= 0) && (v < 3))
		x = v;
	else if (v == 4)
		x = 3;
	else if (v == 8)
		x = 4;
	else if (v == 16)
		x = 5;
	else
		return "Invalid argument #2.";
	if (0 == strcmp(args[0],"tos")) {
		m_sampmod &= 0x1f;
		m_sampmod |= x << 5;
	} else if (0 == strcmp(args[0],"pos")) {
		m_sampmod &= 0xe3;
		m_sampmod |= x << 2;
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}


const char *BME280::exeCmd(Terminal &term, int argc, const char **args)
{
	if ((argc == 0) || ((argc == 1) && (0 == strcmp(args[0],"-h")))) {
		BMP280::exeCmd(term,argc,args);
		term.println("hos <m>: for humidity");
		return 0;
	}
	if (argc == 1) {
		uint8_t s;
		if (0 == strcmp(args[0],"hos")) {
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,BME280_REG_CTRLHUM,&s,sizeof(s)))
				return esp_err_to_name(e);
		} else {
			return BMP280::exeCmd(term,argc,args);
		}
		term.printf("x%u\n",s != 0 ? 1<<--s : 0);
		return 0;
	}
	if (argc != 2)
		return "Invalid number of arguments.";
	char *e;
	long v = strtol(args[1],&e,0);
	if (*e)
		return "Invalid argument #2.";
	uint8_t x;
	if ((v >= 0) && (v < 3))
		x = v;
	else if (v == 4)
		x = 3;
	else if (v == 8)
		x = 4;
	else if (v == 16)
		x = 5;
	else
		return "Invalid argument #2.";
	if (0 == strcmp(args[0],"tos")) {
		m_sampmod &= 0x1f;
		m_sampmod |= x << 5;
	} else if (0 == strcmp(args[0],"pos")) {
		m_sampmod &= 0xe3;
		m_sampmod |= x << 2;
	} else if (0 == strcmp(args[0],"hos")) {
		if (esp_err_t e = i2c_write2(m_bus, m_addr, BME280_REG_CTRLHUM, x))
			return esp_err_to_name(e);
	} else {
		return "Invalid argument #1.";
	}
	return 0;
}
#endif


void BMP280::handle_error()
{
	m_temp->set(NAN);
	m_press->set(NAN);
}


void BME280::handle_error()
{
	m_humid->set(NAN);
	BMP280::handle_error();
}


int32_t BMP280::calc_tfine(uint8_t *data)
{
	int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
	int32_t var1, var2;
	var1 = ((((adc_T>>3) - ((int32_t)T1<<1))) * ((int32_t)T2)) >> 11;
	var2 = (((((adc_T>>4) - ((int32_t)T1)) * ((adc_T>>4) - ((int32_t)T1))) >> 12) * ((int32_t)T3)) >> 14;
	return var1 + var2;
}


#ifdef USE_DOUBLE
float BMP280::calc_press(int32_t adc_P, int32_t t_fine)
{
	double var1, var2, p;
	var1 = ((double)t_fine/2.0) - 64000.0;
	var2 = var1 * var1 * ((double)P6) / 32768.0;
	var2 = var2 + var1 * ((double)P5) * 2.0;
	var2 = (var2/4.0)+(((double)P4) * 65536.0);
	var1 = (((double)P3) * var1 * var1 / 524288.0 + ((double)P2) * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0)*((double)P1);
	if (var1 == 0.0)
		return 0; // avoid exception caused by division by zero
	p = 1048576.0 - (double)adc_P;
	p = (p - (var2 / 4096.0)) * 6250.0 / var1;
	var1 = ((double)P9) * p * p / 2147483648.0;
	var2 = p * ((double)P8) / 32768.0;
	p = p + (var1 + var2 + ((double)P7)) / 16.0;
	return p;
}


float BME280::calc_humid(int32_t adc_H, int32_t t_fine)
{
	double var_H;
	var_H = (((double)t_fine) - 76800.0);
	var_H = (adc_H - (((double)H4) * 64.0 + ((double)H5) / 16384.0 * var_H)) *
		(((double)H2) / 65536.0 * (1.0 + ((double)H6) / 67108864.0 * var_H *
			(1.0 + ((double)H3) / 67108864.0 * var_H)));
	var_H = var_H * (1.0 - ((double)H1) * var_H / 524288.0);
	if (var_H > 100.0)
		var_H = 100.0;
	else if (var_H < 0.0)
		var_H = 0.0;
	return var_H;
}

#else

float BMP280::calc_press(int32_t adc_P, int32_t t_fine)
{
	int64_t var1, var2, p;
	var1 = ((int64_t)t_fine) - 128000;
	var2 = var1 * var1 * (int64_t)P6;
	var2 = var2 + ((var1*(int64_t)P5)<<17);
	var2 = var2 + (((int64_t)P4)<<35);
	var1 = ((var1 * var1 * (int64_t)P3)>>8) + ((var1 * (int64_t)P2)<<12);
	var1 = (((((int64_t)1)<<47)+var1))*((int64_t)P1)>>33;
	if (var1 == 0) {
		m_press = 0;
		log_dbug(TAG,"press: div by 0");
		return NAN; // avoid exception caused by division by zero
	}
	p = 1048576-adc_P;
	p = (((p<<31)-var2)*3125)/var1;
	var1 = (((int64_t)P9) * (p>>13) * (p>>13)) >> 25;
	var2 = (((int64_t)P8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((int64_t)P7)<<4);
	return (float)p/25600.0;	// mBar,hPa
}


float BME280::calc_humid(int32_t adc_H, int32_t t_fine)
{
	t_fine -= 76800;
	adc_H <<= 14;
	int32_t x1 = (((adc_H - (((int32_t)H4) << 20) - (((int32_t)H5) * t_fine)) + 16384) >> 15)
		* (((((((t_fine * (int32_t)H6) >> 10) * (((t_fine * (int32_t)H3) >> 11) + 32768)) >> 10) + ((int32_t)2097152)) * ((int32_t)H2) + 8192) >> 14);
	int32_t x2 = x1 >> 15;
	x1 -= (((x2 * x2) >> 7) * (int32_t)H1) >> 4;
	if (x1 < 0)
		x1 = 0;
	if (x1 > 419430400)
		x1 = 419430400;
	return ((float)(uint32_t)(x1>>12))/1024.0;
}

#endif	// USE_DOUBLE


int BMP280::sample()
{
	// bit 7-5: temperature oversampling:	001=1x, ... 101=16x
	// bit 4-2: pressure oversampling:	001=1x, ... 101=16x
	// bit 1,0: sensor mode			00: sleep, 01/10: force, 11: normal
	// ctrl_meas: t*1, p*1, force
	uint8_t cmd[] = { m_addr, BME280_REG_CTRLMEAS, m_sampmod };
	return i2c_write(m_bus, cmd, sizeof(cmd), true, true);
}


int BMP280::read()
{
	uint8_t data[6];
	if (int r = i2c_w1rd(m_bus,m_addr,BMX280_REG_BASE,data,sizeof(data)))
		return r;
	int32_t t_fine = calc_tfine(data);
	float t = (float)((t_fine * 5 + 128) >> 8) / 100.0;
	float p = calc_press((data[0] << 12) | (data[1] << 4) | (data[2] >> 4), t_fine);
	m_temp->set(t);
	m_press->set(p);
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_FLOAT_NANO
	log_dbug(TAG,"t=%G, p=%G",t,p);
#else
	if (log_module_enabled(TAG)) {
		char ts[16],ps[16];
		float_to_str(ts,t);
		float_to_str(ps,p);
		log_dbug(TAG,"t=%s,p=%s",ts,ps);
	}
#endif
	return 0;
}


int BME280::sample()
{
	uint8_t cmd[] =
		// x1 sampling of humidity
		{ m_addr
		 // bit 7-5: temperature oversampling:	001=1x, ... 101=16x
		 // bit 4-2: pressure oversampling:	001=1x, ... 101=16x
		 // bit 1,0: sensor mode		00: sleep, 01/10: force, 11: normal
		, BME280_REG_CTRLMEAS, 0b00100101
	};
	return i2c_write(m_bus, cmd, sizeof(cmd), true, true);
}


int BME280::read()
{
	uint8_t data[8] = {0};
	if (int r = i2c_w1rd(m_bus,m_addr,BMX280_REG_BASE,data,sizeof(data)))
		return r;
	int32_t t_fine = calc_tfine(data);
	float t = (float)((t_fine * 5 + 128) >> 8) / 100.0;
	m_temp->set(t);
	float p = calc_press((data[0] << 12) | (data[1] << 4) | (data[2] >> 4), t_fine);
	m_press->set(p);
	float h = calc_humid((data[6] << 8) | data[7], t_fine);
	m_humid->set(h);
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_FLOAT_NANO
	log_dbug(TAG,"t=%G, p=%G, h=%G",t,p,h);
#else
	if (log_module_enabled(TAG)) {
		char ts[16], ps[16], hs[16];
		float_to_str(ts,t);
		float_to_str(ps,p);
		float_to_str(hs,h);
		log_dbug(TAG,"t=%s,p=%s,h=%s",ts,ps,hs);
	}
#endif
	return 0;
}


bool BMP280::status()
{
	uint8_t status = 0;
	if (i2c_w1rd(m_bus,m_addr,BME280_REG_STATUS,&status,1))
		return true;
	if ((status & 0x8) == 0)
		m_state = st_read;
	return false;
}


int BMP280::init()
{
	int r;
	r = i2c_write2	(m_bus, m_addr
			, BME280_REG_CONFIG, m_cfg
		);
	if (r)
		return r;
	uint8_t calib[26];
	r = i2c_w1rd(m_bus,m_addr,BME280_CALIB_DATA,calib,sizeof(calib));
	if (r)
		return r;
	T1 = (calib[1] << 8) | calib[0];
	T2 = (calib[3] << 8) | calib[2];
	T3 = (calib[5] << 8) | calib[4];
	P1 = (calib[7] << 8) | calib[6];
	P2 = (calib[9] << 8) | calib[8];
	P3 = (calib[11] << 8) | calib[10];
	P4 = (calib[13] << 8) | calib[12];
	P5 = (calib[15] << 8) | calib[14];
	P6 = (calib[17] << 8) | calib[16];
	P7 = (calib[19] << 8) | calib[18];
	P8 = (calib[21] << 8) | calib[20];
	P9 = (calib[23] << 8) | calib[22];
	H1 = calib[25];
	return 0;
}


int BME280::init()
{
	if (BMP280::init())
		return 1;
	int r;
	r = i2c_write2(m_bus, m_addr, BME280_REG_CTRLHUM, 1);
	if (r)
		return r;

	uint8_t calib_h[7];
	r = i2c_w1rd(m_bus,m_addr,BME280_REG_CALIB,calib_h,sizeof(calib_h));
	if (r)
		return r;
	H2 = (int16_t)(calib_h[1] << 8) | calib_h[0];
	H3 = calib_h[2];
	H4 = ((int16_t)(int8_t)calib_h[3] << 4) | ((int16_t)calib_h[4] & 0xf);
	H5 = (int16_t)(calib_h[4] >> 4) | (calib_h[5] << 4);
	H6 = (int8_t)calib_h[6];
	return 0;
}
#endif	// CONFIG_BMX280



#ifdef CONFIG_BME680
int BME680::init()
{
	m_temp = new EnvNumber("temperature","\u00b0C","%4.1f");
	m_press = new EnvNumber("pressure","hPa","%4.1f");
	m_humid = new EnvNumber("humidity","%","%4.1f");
	m_gas = new EnvNumber("gasresistance","kOhm","%4.1f");
	bzero(&m_dev,sizeof(m_dev));
	m_dev.bus = m_bus;
	m_dev.addr = m_addr;
	m_dev.amb_temp = 25;	// default for gas sensor init
	if (int r = bme680_init(&m_dev))
		return r;
	m_dev.tph_sett.os_hum = BME680_OS_2X;
	m_dev.tph_sett.os_pres = BME680_OS_4X;
	m_dev.tph_sett.os_temp = BME680_OS_8X;
	m_dev.tph_sett.filter = BME680_FILTER_SIZE_3;
	m_dev.gas_sett.run_gas = BME680_ENABLE_GAS_MEAS;
	m_dev.gas_sett.heatr_temp = 320;	// in deg C
	m_dev.gas_sett.heatr_dur = 150;		// in ms
	m_dev.power_mode = BME680_FORCED_MODE;
	return bme680_set_sensor_settings(BME680_OST_SEL|BME680_OSP_SEL|BME680_OSH_SEL|BME680_FILTER_SEL|BME680_RUN_GAS_SEL|BME680_GAS_MEAS_SEL, &m_dev);
}


unsigned BME680::cyclic(void *arg)
{
	BME680 *dev = (BME680 *) arg;
	switch (dev->m_state) {
	case st_idle:
		return 20;
	case st_sample:
		return dev->sample();
	case st_read:
		return dev->read();
	case st_error:
		dev->m_temp->set(NAN);
		dev->m_press->set(NAN);
		dev->m_humid->set(NAN);
		dev->m_gas->set(NAN);
		dev->m_state = st_idle;
		return 1000;
	default:
		abort();
		return 0;
	}
}


void BME680::trigger(void *arg)
{
	BME680 *dev = (BME680 *)arg;
	if (dev->m_state == st_idle)
		dev->m_state = st_sample;
}


unsigned BME680::sample()
{
	bme680_set_sensor_mode(&m_dev);
	uint16_t meas_period;
	bme680_get_profile_dur(&meas_period,&m_dev);
	m_state = st_read;
	return meas_period;
}


unsigned BME680::read()
{
	if (m_temp == 0) {
		log_dbug(TAG,"read on non-attached device");
		return 50;
	}
	uint8_t status = 0;
	int r = i2c_w1rd(m_bus,m_addr,0x1d,&status,1);
	if (r != 0) {
		m_state = st_error;
		return 50;
	}
	if (status & 0x60)
		return 5;
	struct bme680_field_data data;
	int8_t e = bme680_get_sensor_data(&data,&m_dev);
	if (e) {
		if (e == BME680_POLL_PERIOD_MS)
			return BME680_POLL_PERIOD_MS;
		m_state = st_error;
		return 20;
	}
#ifdef BME680_FLOAT_POINT_COMPENSATION
	m_temp->set(data.temperature);
	m_press->set(data.pressure);
	m_humid->set(data.humidity);
#else
	m_temp->set((float)data.temperature/100.0);
	m_press->set((float)data.pressure/100.0);
	m_humid->set((float)data.humidity/1000.0);
#endif
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_FLOAT_NANO
	log_dbug(TAG,"t=%G, p=%G, h=%G",m_temp->get(),m_press->get(),m_humid->get());
#else
	if (log_module_enabled(TAG)) {
		char t[8],p[8],h[8];
		float_to_str(t,m_temp->get());
		float_to_str(p,m_press->get());
		float_to_str(h,m_humid->get());
		log_dbug(TAG,"t=%s,p=%s,h=%s",t,p,h);
	}
#endif
	m_state = st_idle;
	if (data.status & BME680_GASM_VALID_MSK) {
		log_dbug(TAG,"r=%d", data.gas_resistance);
		m_gas->set((float)data.gas_resistance/1000.0);
	} else {
		log_dbug(TAG,"gas invalid");
		m_gas->set(NAN);
	}
	return 50;
}


void BME680::attach(EnvObject *root)
{
	root->add(m_temp);
	root->add(m_press);
	root->add(m_humid);
	root->add(m_gas);
	cyclic_add_task(m_name,cyclic,this,0);
	action_add(concat(m_name,"!sample"),trigger,(void*)this,"BME680 sample data");
}
#endif // CONFIG_BME680


static int create_device(uint8_t bus, uint8_t addr, uint8_t id)
{
	I2CDevice *s = 0;
	switch (id) {
#ifdef CONFIG_BMX280
	case 0x58:
		s = new BMP280(bus,addr);
		break;
	case 0x60:
		s = new BME280(bus,addr);
		break;
#endif
#ifdef CONFIG_BME680
	case 0x61:
		s = new BME680(bus,addr);
		break;
#endif
	default:
		log_dbug(TAG,"no driver for ID 0x%x",id);
		return 0;
	}
	s->init();
	return 1;
}


unsigned bmx_scan(uint8_t port)
{
	// 7bit			=     8bit with R/W
	// (0x76,0x77) << 1) | R/_W = 0xec/0xee
	unsigned num = 0;
	uint8_t addr = BME_ADDR_MIN;
	do {
		uint8_t id = 0;
		int r = i2c_w1rd(port,addr,BME_REG_ID,&id,sizeof(id));
		// esp32 i2c stack has a bug and reports timeout
		// although data was received correctly...
		// so ignore return codes > 0
		if ((r >= 0) && (id != 0))
			num += create_device(port,addr,id);
		addr += 2;
	} while (addr <= BME_ADDR_MAX);
	return num;
}

