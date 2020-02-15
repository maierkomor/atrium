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

#ifdef CONFIG_BME280

//#define USE_DOUBLE

#include <driver/i2c.h>
#include <esp_err.h>
#include <strings.h>

#include "bme280.h"
#include "log.h"

#define BME280_REG_ID		0xd0
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
#define BME280_REG_CONFIG	0Xf5
#define BME280_REG_CTRLMEAS	0xf4

#define I2C_NUM 0

#define BME280_I2C_ADDR 0xec
#define BME280_READ 1
#define BME280_WRITE 0

#define BME280_START() bme280_write1(0xf4,0xb5)
#define BME280_RESET() bme280_write1(0xe0,0xb6)

static const char TAG[] = "BME";
static uint16_t dig_T1;
static int16_t dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t dig_H1, dig_H3;
static int16_t dig_H2, dig_H4, dig_H5;
static int8_t dig_H6;


static int bme280_read_registers(uint8_t id, uint8_t r, uint8_t *d, uint8_t n)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, BME280_I2C_ADDR | (id<<1) | BME280_WRITE, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, r, I2C_MASTER_NACK);
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, BME280_I2C_ADDR | (id<<1) | BME280_READ, I2C_MASTER_NACK);
	i2c_master_read(cmd, d, n, I2C_MASTER_LAST_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin(I2C_NUM, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	return ret;
}


static int bme280_write1(uint8_t id, uint8_t r, uint8_t v)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, BME280_I2C_ADDR | (id<<1) | BME280_WRITE, I2C_MASTER_ACK);
	i2c_master_write_byte(cmd, r, I2C_MASTER_ACK);
	i2c_master_write_byte(cmd, v, I2C_MASTER_ACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin(I2C_NUM, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	return ret;
}


static int bme280_write2(uint8_t id, uint8_t r0, uint8_t v0, uint8_t r1, uint8_t v1)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, BME280_I2C_ADDR | (id<<1) | BME280_WRITE, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, r0, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, v0, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, r1, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, v1, I2C_MASTER_NACK);
	i2c_master_stop(cmd);
	int ret = i2c_master_cmd_begin(I2C_NUM, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	return ret;
}

static int32_t calc_tfine(int32_t adc_T)
{
	int32_t var1, var2;
	var1 = ((((adc_T>>3) - ((int32_t)dig_T1<<1))) * ((int32_t)dig_T2)) >> 11;
	var2 = (((((adc_T>>4) - ((int32_t)dig_T1)) * ((adc_T>>4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
	return var1 + var2;
}


#ifdef USE_DOUBLE
static double compensate_P_double(int32_t adc_P, int32_t t_fine)
{
	double var1, var2, p;
	var1 = ((double)t_fine/2.0) - 64000.0;
	var2 = var1 * var1 * ((double)dig_P6) / 32768.0;
	var2 = var2 + var1 * ((double)dig_P5) * 2.0;
	var2 = (var2/4.0)+(((double)dig_P4) * 65536.0);
	var1 = (((double)dig_P3) * var1 * var1 / 524288.0 + ((double)dig_P2) * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0)*((double)dig_P1);
	if (var1 == 0.0)
		return 0; // avoid exception caused by division by zero
	p = 1048576.0 - (double)adc_P;
	p = (p - (var2 / 4096.0)) * 6250.0 / var1;
	var1 = ((double)dig_P9) * p * p / 2147483648.0;
	var2 = p * ((double)dig_P8) / 32768.0;
	p = p + (var1 + var2 + ((double)dig_P7)) / 16.0;
	return p;
}


static double compensate_H_double(int32_t adc_H, int32_t t_fine)
{
	double var_H;
	var_H = (((double)t_fine) - 76800.0);
	var_H = (adc_H - (((double)dig_H4) * 64.0 + ((double)dig_H5) / 16384.0 * var_H)) *
		(((double)dig_H2) / 65536.0 * (1.0 + ((double)dig_H6) / 67108864.0 * var_H *
			(1.0 + ((double)dig_H3) / 67108864.0 * var_H)));
	var_H = var_H * (1.0 - ((double)dig_H1) * var_H / 524288.0);
	if (var_H > 100.0)
		var_H = 100.0;
	else if (var_H < 0.0)
		var_H = 0.0;
	return var_H;
}

#else

static uint32_t calc_press(int32_t adc_P, int32_t t_fine)
{
	int64_t var1, var2, p;
	var1 = ((int64_t)t_fine) - 128000;
	var2 = var1 * var1 * (int64_t)dig_P6;
	var2 = var2 + ((var1*(int64_t)dig_P5)<<17);
	var2 = var2 + (((int64_t)dig_P4)<<35);
	var1 = ((var1 * var1 * (int64_t)dig_P3)>>8) + ((var1 * (int64_t)dig_P2)<<12);
	var1 = (((((int64_t)1)<<47)+var1))*((int64_t)dig_P1)>>33;
	if (var1 == 0)
		return 0; // avoid exception caused by division by zero
	p = 1048576-adc_P;
	p = (((p<<31)-var2)*3125)/var1;
	var1 = (((int64_t)dig_P9) * (p>>13) * (p>>13)) >> 25;
	var2 = (((int64_t)dig_P8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7)<<4);
	return (uint32_t) p;
}


static uint32_t calc_humid(int32_t adc_H, int32_t t_fine)
{
	t_fine -= 76800;
	adc_H <<= 14;
	int32_t x1 = (((adc_H - (((int32_t)dig_H4) << 20) - (((int32_t)dig_H5) * t_fine)) + 16384) >> 15)
		* (((((((t_fine * (int32_t)dig_H6) >> 10) * (((t_fine * (int32_t)dig_H3) >> 11) + 32768)) >> 10) + ((int32_t)2097152)) * ((int32_t)dig_H2) + 8192) >> 14);
	int32_t x2 = x1 >> 15;
	x1 -= (((x2 * x2) >> 7) * (int32_t)dig_H1) >> 4;
	if (x1 < 0)
		x1 = 0;
	if (x1 > 419430400)
		x1 = 419430400;
	return (uint32_t)(x1>>12);
}
#endif


int bme280_read(double *t, double *h, double *p)
{
	uint8_t data[8];
	bzero(data,sizeof(data));
	bme280_write1( 0
			/*
			 * bit 7-5: temperature oversampling:	001=1x, ... 101=16x
			 * bit 4-2: pressure oversampling:	001=1x, ... 101=16x
			 * bit 1,0: sensor mode			00: sleep, 01/10: force, 11: normal
			 */
			, BME280_REG_CTRLMEAS, 0b00100101);	// ctrl_meas: t*1, p*1, force
	if (bme280_read_registers(0,0xf7,data,sizeof(data)))
		return 1;
#ifdef USE_DOUBLE
	int32_t t_fine = calc_tfine((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));
	*t = ((double)t_fine * 5 + 128) / 256.0 / 100.0;
	*h = compensate_P_double((data[0] << 12) | (data[1] << 4) | (data[2] >> 4), t_fine);
	*p = compensate_H_double((data[6] << 8) | data[7], t_fine);
#else
	int32_t t_fine = calc_tfine((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));
	*t = (double)((t_fine * 5 + 128) >> 8) / 100.0;
	*p = (double)calc_press((data[0] << 12) | (data[1] << 4) | (data[2] >> 4), t_fine)/256.0;
	*h = (double)calc_humid((data[6] << 8) | data[7], t_fine)/1024.0;
#endif
	return 0;
}


uint8_t bme280_status()
{
	uint8_t d;
	esp_err_t e = bme280_read_registers(0,0xf3,&d,1);
	if (e) {
		log_error(TAG,"read status failed: %d",e);
		return 0;
	}
	return d;
}


int bme280_init(unsigned sda, unsigned scl)
{
	i2c_config_t conf;
	conf.mode = I2C_MODE_MASTER;
	conf.sda_io_num = sda;
	conf.sda_pullup_en = 0;
	conf.scl_io_num = scl;
	conf.scl_pullup_en = 0;
	esp_err_t e = i2c_driver_install(I2C_NUM_0, conf.mode);
	if (e)
		return e;
	e = i2c_param_config(I2C_NUM_0, &conf);
	if (e)
		return e;
	log_info(TAG,"status %d",bme280_status());
	uint8_t id = 0;
	int r = bme280_read_registers(0,0xd0,&id,1);
	if (r) {
		log_error(TAG,"read register failed: %s",esp_err_to_name(r));
		return 1;
	}
	if (id != 0x60) {
		log_error(TAG,"got unexpected id 0x%x",id);
		return 2;
	}

	bme280_write1	( 0

			/*
			 * bit 7-5: sampling time interval: 101=1000ms
			 * bit 4-2: iir filter time
			 * bit 0:   1=3-wire spi interace
			 */
			, BME280_REG_CONFIG, 0b10100000		// config   : 1000ms sampling, filter off, 3-wire disable
		);
	uint8_t calib[26];
	r = bme280_read_registers(0,0x88,calib,sizeof(calib));
	if (r) 
		log_error(TAG,"error reading calib 0-25: %d",r);
	dig_T1 = (calib[1] << 8) | calib[0];
	dig_T2 = (calib[3] << 8) | calib[2];
	dig_T3 = (calib[5] << 8) | calib[4];
	dig_P1 = (calib[7] << 8) | calib[6];
	dig_P2 = (calib[9] << 8) | calib[8];
	dig_P3 = (calib[11] << 8) | calib[10];
	dig_P4 = (calib[13] << 8) | calib[12];
	dig_P5 = (calib[15] << 8) | calib[14];
	dig_P6 = (calib[17] << 8) | calib[16];
	dig_P7 = (calib[19] << 8) | calib[18];
	dig_P8 = (calib[21] << 8) | calib[20];
	dig_P9 = (calib[23] << 8) | calib[22];
	dig_H1 = calib[25];

	uint8_t calib_h[7];
	r = bme280_read_registers(0,0xe1,calib_h,sizeof(calib_h));
	if (r) 
		log_error(TAG,"error reading calib_h: %d",r);
	dig_H2 = (calib_h[1] << 8) | calib_h[0];
	dig_H3 = calib_h[2];
	dig_H4 = (calib_h[3] << 4) | (calib_h[4] & 0xf);
	dig_H5 = (calib_h[4] >> 4) | (calib_h[5] << 4);
	dig_H6 = (int8_t)calib_h[6];

	return 0;
}


#endif
