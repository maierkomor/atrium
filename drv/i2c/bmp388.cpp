/*
 *  Copyright (C) 2023-2024, Thomas Maier-Komor
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

// This drivers operates the BMP388 in I2C mode.
// Connect CSB to 3.3V to enable I2C mode.
// Connect SDO to either GND or VCC to select the base address.

#ifdef CONFIG_BMP388

#include <esp_err.h>
#include <strings.h>

#include "actions.h"
#include "bmp388.h"
#include "cyclic.h"
#include "event.h"
#include "i2cdrv.h"
#include "log.h"
#include "stream.h"
#include "terminal.h"
#include "xio.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define BMP388_ADDR	0x58

#define ADDR_MIN	(0x76<<1)
#define ADDR_MAX	(0x77<<1)

#define REG_ID		0x00
#define REG_ERROR	0x02
#define REG_STATUS	0x03
#define REG_DATA	0x04
#define REG_EVENT	0x10
#define REG_INT_CTRL	0x19
#define REG_PWR_CTRL	0x1b
#define REG_OSR		0x1c
#define REG_ODR		0x1d
#define REG_CONFIG	0x1f	// IIR filter
#define REG_CMD		0x7e
#define REG_BASE	0x2

#define CMD_FIFO_FLUSH	0xb0
#define CMD_RESET	0xb6
#define CMD_EXTMODE_EN	0x34

#define BIT_ERR_FATAL	0x1
#define BIT_ERR_CMD	0x2
#define BIT_ERR_CONF	0x4

#define BIT_ST_CRDY	0x10
#define BIT_ST_PRDY	0x20
#define BIT_ST_TRDY	0x40

#define BIT_PWR_PON	0x1
#define BIT_PWR_TON	0x2
#define BIT_PWR_FORCE	0x10
#define BIT_PWR_NORM	0x30

#define BIT_INT_OD	0x1	// open-drain port
#define BIT_INT_AHI	0x2	// active high interrupts
#define BIT_INT_LATCH	0x4	// latch interrupts in status reg
#define BIT_INT_FIFO	0x8	// FIFO watermark interrupt
#define BIT_INT_FULL	0x10	// FIFO full interrupt
#define BIT_INT_DRDY	0x40	// data ready

#define T1 0
#define T2 1
#define T3 2
#define P1 3
#define P2 4
#define P3 5
#define P4 6
#define P5 7
#define P6 8
#define P7 9
#define P8 10
#define P9 11
#define P10 12
#define P11 13

#define CALIB_DATA	0x31
#define BMX280_REG_BASE	0xf7

#define TAG MODULE_BMX


BMP388::BMP388(uint8_t port, uint8_t addr, const char *n)
: I2CDevice(port,addr,n ? n : drvName())
, m_temp("temperature","\u00b0C","%4.1f")
, m_press("pressure","hPa","%4.1f")
{
}


void BMP388::addIntr(uint8_t intr)
{
#if 0
	// TODO: currently incomplete
	event_t irqev = event_register(m_name,"`irq");
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = xio_cfg_pull_none;
	cfg.cfg_intr = xio_cfg_intr_fall;
	if (0 > xio_config(intr,cfg)) {
		log_warn(TAG,"config interrupt error");
	} else if (esp_err_t e = xio_set_intr(intr,event_isr_handler,(void*)(unsigned)irqev)) {
		log_warn(TAG,"error attaching interrupt: %s",esp_err_to_name(e));
	} else {
		log_info(TAG,"BMP388@%u,0x%x: interrupt on GPIO%u",m_bus,m_addr,intr);
	}
#endif
}


void BMP388::trigger(void *arg)
{
	BMP388 *dev = (BMP388 *)arg;
	if (dev->m_state == st_idle)
		dev->m_state = st_sample;
}


unsigned BMP388::cyclic(void *arg)
{
	BMP388 *drv = (BMP388 *) arg;
	switch (drv->m_state) {
	case st_idle:
		return 20;
	case st_sample:
		if (drv->sample())
			break;
		drv->m_state = st_measure;
		return 85;	// conversion time depends on oversampling
				// 85ms covers all cases
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


void BMP388::attach(EnvObject *root)
{
	if (init())
		return;
	root->add(&m_temp);
	root->add(&m_press);
	cyclic_add_task(m_name,BMP388::cyclic,this,0);
	action_add(concat(m_name,"!sample"),trigger,(void*)this,"BMP388 sample data");
}


#ifdef CONFIG_I2C_XCMD

int osr_value(uint8_t v)
{
	if (v == 0)
		return -1;
	int r = 0;
	while ((v & 1) == 0) {
		++r;
		v >>= 1;
	}
	if (v & ~1)
		return -1;
	return r;
}


const char *BMP388::exeCmd(Terminal &term, int argc, const char **args)
{
	static const uint8_t coef[] = {0,1,3,7,15,31,63,127};

	if (argc == 1) {
		if (0 == strcmp(args[0],"-h")) {
			term.println(
				"iir [<val>]: set IIR value\n"
				"osr        : read over-sampling rates\n"
				"osrt <val> : set over-sampling rate for temperature\n"
				"osrp <val> : set over-sampling rate for pressure\n"
				);
		} else if (0 == strcmp(args[0],"iir")) {
			uint8_t iir;
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_CONFIG,&iir,sizeof(iir)))
				return esp_err_to_name(e);
			iir >>= 1;
			if (iir >= sizeof(coef)/sizeof(coef[0])) {
//				term.printf("IIR register value %d\n",iir);
				return "Unexpted register value.";
			} else {
				term.printf("IIR filter cofficient %d\n",coef[iir]);
			}
		} else if (0 == strcmp(args[0],"osr")) {
			uint8_t osr;
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_OSR,&osr,sizeof(osr)))
				return esp_err_to_name(e);
			term.printf("oversampling: temperatur x%u, pressure x%u\n",1<<((osr>>3)&7),1<<(osr&7));
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 2) {
		if (0 == strcmp(args[0],"iir")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e)
				return "Invalid argument #2.";
			int c = -1;
			for (int x = 0; x < sizeof(coef)/sizeof(coef[0]); ++x) {
				if (l == coef[x]) {
					c = x;
					break;
				}
			}
			if (c == -1) 
				return "Invalid argument #2.";
			if (esp_err_t e = i2c_write2(m_bus,m_addr,REG_CONFIG,c<<1))
				return esp_err_to_name(e);
		} else if (0 == strcmp(args[0],"osrp")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if ((*e != 0) || (l <= 0) || (l > 32))
				return "Invalid argument #2.";
			int v = osr_value(l);
			if (v < 0)
				return "Invalid argument #2.";
			uint8_t osr;
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_OSR,&osr,sizeof(osr)))
				return esp_err_to_name(e);
			osr &= 0x38;
			osr |= v;
			if (esp_err_t e = i2c_write2(m_bus,m_addr,REG_OSR,osr))
				return esp_err_to_name(e);
		} else if (0 == strcmp(args[0],"osrt")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if ((*e != 0) || (l <= 0) || (l > 32))
				return "Invalid argument #2.";
			int v = osr_value(l);
			if (v < 0)
				return "Invalid argument #2.";
			uint8_t osr;
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_OSR,&osr,sizeof(osr)))
				return esp_err_to_name(e);
			osr &= 0x7;
			osr |= v << 3;
			if (esp_err_t e = i2c_write2(m_bus,m_addr,REG_OSR,osr))
				return esp_err_to_name(e);
		} else {
			return "Invalid argument #1.";
		}
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}
#endif


int BMP388::get_error()
{
	uint8_t err = 0;
	if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_ERROR,&err,sizeof(err)))
		return -e;
	if (err)
		log_warn(TAG,"device error %d",err);
	return err;
}


int BMP388::get_status()
{
	uint8_t status = 0;
	if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_ERROR,&status,sizeof(status)))
		return -e;
	log_dbug(TAG,"device status %d",status);
	return status;
}


void BMP388::handle_error()
{
	m_temp.set(NAN);
	m_press.set(NAN);
}


void BMP388::calc_tfine(uint32_t uncomp_temp)
{
	log_dbug(TAG,"T1=%g, T2=%g, T3=%g",D[T1],D[T2],D[T3]);
	float partial_data1 = (float)(uncomp_temp - D[T1]);
	float partial_data2 = (float)(partial_data1 * D[T2]);
	m_temp.set(partial_data2 + (partial_data1 * partial_data1) * D[T3]);
}


void BMP388::calc_press(uint32_t uncomp_press)
{
	float temp = m_temp.get();
	float temp_2 = temp * temp;
	float temp_3 = temp_2 * temp;
	float partial_data1 = D[P6] * temp;
	float partial_data2 = D[P7] * temp_2;
	float partial_data3 = D[P8] * temp_3;
	float partial_out1 = D[P5] + partial_data1 + partial_data2 + partial_data3;
	partial_data1 = D[P2] * temp;
	partial_data2 = D[P3] * temp_2;
	partial_data3 = D[P4] * temp_3;
	float partial_out2 = (float)uncomp_press * (D[P1] + partial_data1 + partial_data2 + partial_data3);
	partial_data1 = (float)uncomp_press * (float)uncomp_press;
	partial_data2 = D[P9] + D[P10] * temp;
	partial_data3 = partial_data1 * partial_data2;
	float partial_data4 = partial_data3 + ((float)uncomp_press * (float)uncomp_press * (float)uncomp_press) * D[P11];
	float comp_press = partial_out1 + partial_out2 + partial_data4;
	m_press.set(comp_press/100.0);
}


int BMP388::sample()
{
	log_dbug(TAG,"sample");
	uint8_t cmd[] = { m_addr, REG_PWR_CTRL, BIT_PWR_FORCE|BIT_PWR_TON|BIT_PWR_PON};
	return i2c_write(m_bus, cmd, sizeof(cmd), true, true);
}


int BMP388::read()
{
	uint8_t data[8];
	if (int r = i2c_w1rd(m_bus,m_addr,REG_BASE,data,sizeof(data)))
		return r;
	log_hex(TAG,data,sizeof(data),"data read:");
	if (data[0]) {
		log_warn(TAG,"device error %x",data[0]);
		return 1;
	}
	if ((data[1] & (BIT_ST_PRDY|BIT_ST_TRDY)) != (BIT_ST_PRDY|BIT_ST_TRDY)) {
		log_warn(TAG,"data not ready");
		return 1;
	}
	log_dbug(TAG,"status: temp %s, press %s, conf %s, cmd %s, %s"
			,data[1]&0x40?"ready":"busy"
			,data[1]&0x20?"ready":"busy"
			,data[0]&0x4?"err":"ok"
			,data[0]&0x2?"err":"ok"
			,data[0]&0x1?"fatal":"ok"
		);
	uint32_t tempraw = ((uint32_t)data[7]<<16) | ((uint32_t)data[6]<<8) | (uint32_t)data[5];
	log_dbug(TAG,"tempraw = %u",tempraw);
	calc_tfine(tempraw);
	uint32_t pressraw = ((uint32_t)data[4]<<16) | ((uint32_t)data[3]<<8) | (uint32_t)data[2];
	calc_press(pressraw);
#ifdef CONFIG_NEWLIB_LIBRARY_LEVEL_FLOAT_NANO
	log_dbug(TAG,"t=%G, p=%G",m_temp.get(),m_press.get());
#else
	if (log_module_enabled(TAG)) {
		char ts[16],ps[16];
		float_to_str(ts,m_temp.get());
		float_to_str(ps,m_press.get());
		log_dbug(TAG,"t=%s,p=%s",ts,ps);
	}
#endif
	i2c_write2(m_bus,m_addr,REG_PWR_CTRL,0);
	return 0;
}


int BMP388::flush_fifo()
{
	return i2c_write2(m_bus,m_addr,REG_CMD,CMD_FIFO_FLUSH);
}


bool BMP388::status()
{
	uint8_t status = 0;
	if (i2c_w1rd(m_bus,m_addr,REG_STATUS,&status,1))
		return true;
	if ((status & 0x8) == 0)
		m_state = st_read;
	return false;
}


int BMP388::init()
{
	if (esp_err_t r = i2c_write2(m_bus, m_addr, REG_CMD, CMD_RESET))
		log_warn(TAG,"failed to reset BMP388@%u,0x%x: %s",m_bus,m_addr,esp_err_to_name(r));
	vTaskDelay(10);
	uint8_t calib[21];
	
	if (esp_err_t r = i2c_w1rd(m_bus,m_addr,CALIB_DATA,calib,sizeof(calib))) {
		log_warn(TAG,"failed to read calibration of BMP388@%u,0x%x: %s",m_bus,m_addr,esp_err_to_name(r));
		return r;
	}
	log_hex(TAG,calib,sizeof(calib),"calib:");
	uint16_t t1 = ((uint16_t)calib[1] << 8) | calib[0];
//	D[T1] = (float)t1 / powf(2,-8);
	D[T1] = (float)t1 / 0.003909625f;
	log_dbug(TAG,"t1 = %d, T1 = %g",t1,D[T1]);

	uint16_t t2 = ((uint16_t)calib[3] << 8) | calib[2];
//	D[T2] = (float)t2 / powf(2,30);
	D[T2] = (float)t2 / 1073741824.0;
	log_dbug(TAG,"t2 = %d, T2 = %g",t2,D[T2]);

	int8_t t3 = (int8_t)calib[4];
//	D[T3] = (float)t3 / powf(2,48);
	D[T3] = (float)t3 / 281474976710656.0;
	log_dbug(TAG,"t3 = %d, T3 = %g",t3,D[T3]);

	int16_t p1 = (calib[6] << 8) | calib[5];
	D[P1] = ((float)p1 - powf(2,14)) / powf(2,20);
	int16_t p2 = (calib[8] << 8) | calib[7];
	D[P2] = ((float)p2 - powf(2,14)) / powf(2,29);
	int8_t p3 = calib[9];
	D[P3] = (float)p3 / powf(2,32);
	int8_t p4 = calib[10];
	D[P4] = (float)p4 / powf(2,37);
	uint16_t p5 = (calib[12] << 8) | calib[11];
	D[P5] = (float)p5 / powf(2,-3);
	uint16_t p6 = (calib[14] << 8) | calib[13];
	D[P6] = (float)p6 / powf(2,6);
	int8_t p7 = calib[15];
	D[P7] = (float)p7 / powf(2,8);
	int8_t p8 = calib[16];
	D[P8] = (float)p8 / powf(2,15);
	int16_t p9 = (calib[18] << 8) | calib[17];
	D[P9] = (float)p9 / powf(2,48);
	int8_t p10 = calib[19];
	D[P10] = (float)p10 / powf(2,48);
	int8_t p11 = calib[20];
	D[P11] = (float)p11 / powf(2,65);
	return 0;
}


unsigned bmp388_scan(uint8_t bus)
{
	unsigned num = 0;
	uint8_t addr = ADDR_MIN;
	do {
		uint8_t id = 0;
		int r = i2c_w1rd(bus,addr,REG_ID,&id,sizeof(id));
		// esp32 i2c stack has a bug and reports timeout
		// although data was received correctly...
		// so ignore return codes > 0
		log_dbug(TAG,"scan BMP388 at 0x%x: %d, id=0x%x",addr,r,id);
		if ((r >= 0) && (id == 0x50)) {
			new BMP388(bus,addr);
			num = 1;
		}
		addr += 2;
	} while (addr <= ADDR_MAX);
	return num;
}

#endif	// CONFIG_BMP388
