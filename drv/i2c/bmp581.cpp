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

// This drivers operates the BMP581 in I2C mode.
// Connect CSB to 3.3V to enable I2C mode.
// Connect SDO to either GND or VCC to select the base address.

#ifdef CONFIG_BMP581

#include <esp_err.h>
#include <strings.h>

#include "actions.h"
#include "bmp581.h"
#include "cyclic.h"
#include "event.h"
#include "i2cdrv.h"
#include "log.h"
#include "stream.h"
#include "terminal.h"
#include "xio.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG MODULE_BMX

#define BMP581_ADDR	0x58

#define ADDR_MIN	(0x46<<1)
#define ADDR_MAX	(0x47<<1)

#define CHIP_ID		0x50

#define REG_CHIP_ID	0x01
#define REG_CHIP_REV	0x02
#define REG_CHIP_STATUS	0x11
#define REG_DRV_CFG	0x13
#define REG_INT_CFG	0x14
#define REG_INT_SRC	0x15
#define REG_FIFO_CFG	0x16
#define REG_FIFO_CNT	0x17
#define REG_FIFO_SEL	0x18
#define REG_TEMP_XLSB	0x1d
#define REG_TEMP_LSB	0x1e
#define REG_TEMP_MSB	0x1f
#define REG_PRESS_XLSB	0x20
#define REG_PRESS_LSB	0x21
#define REG_PRESS_MSB	0x22
#define REG_INT_STATUS	0x27
#define REG_STATUS	0x28
#define REG_FIFO_DATA	0x29
#define REG_NVM_ADDR	0x2b
#define REG_NVM_LSB	0x2c
#define REG_NVM_MSB	0x2d
#define REG_DSP_CFG	0x30
#define REG_DSP_IIR	0x31
#define REG_OOR_P_LSB	0x32
#define REG_OOR_P_MSB	0x33
#define REG_OOR_RANGE	0x34
#define REG_OOR_CFG	0x35
#define REG_OSR_CFG	0x36
#define REG_ODR_CFG	0x37
#define REG_OSR_EFF	0x38
#define REG_CMD		0x7e


BMP581::BMP581(uint8_t port, uint8_t addr, const char *n)
: I2CDevice(port,addr,n ? n : drvName())
, m_temp("temperature","\u00b0C","%4.1f")
, m_press("pressure","hPa","%5.2f")
{
}


void BMP581::addIntr(uint8_t intr)
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
		log_info(TAG,"BMP581@%u,0x%x: interrupt on GPIO%u",m_bus,m_addr,intr);
	}
#endif
}


/*
void BMP581::trigger(void *arg)
{
	BMP581 *dev = (BMP581 *)arg;
	if (dev->m_state == st_idle)
		dev->m_state = st_sample;
}
*/


unsigned BMP581::cyclic(void *arg)
{
	BMP581 *drv = (BMP581 *) arg;
	return drv->cyclic();
}


unsigned BMP581::cyclic()
{
	unsigned delay = 50;
	if (m_restart == 0) {
		read_data();
		if (m_itv > delay) {
			m_restart = m_itv - delay;
		} else {
			delay = m_itv;
		}
	} else if (m_restart > delay) {
		m_restart -= delay;
	} else {
		delay = m_restart;
		m_restart = 0;
	}
	return delay;
}


void BMP581::attach(EnvObject *root)
{
	if (init())
		return;
	root->add(&m_temp);
	root->add(&m_press);
	cyclic_add_task(m_name,BMP581::cyclic,this,0);
	//action_add(concat(m_name,"!sample"),trigger,(void*)this,"BMP581 sample data");
}


#ifdef CONFIG_I2C_XCMD
// oversampling rate - must be a 2^x value
static int osr_value(uint8_t v)
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


const char *BMP581::exeCmd(Terminal &term, int argc, const char **args)
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
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_DSP_CFG,&iir,sizeof(iir)))
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
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_OSR_CFG,&osr,sizeof(osr)))
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
			if (esp_err_t e = i2c_write2(m_bus,m_addr,REG_DSP_CFG,c<<1))
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
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_OSR_CFG,&osr,sizeof(osr)))
				return esp_err_to_name(e);
			osr &= 0x38;
			osr |= v;
			if (esp_err_t e = i2c_write2(m_bus,m_addr,REG_OSR_CFG,osr))
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
			if (esp_err_t e = i2c_w1rd(m_bus,m_addr,REG_OSR_CFG,&osr,sizeof(osr)))
				return esp_err_to_name(e);
			osr &= 0x7;
			osr |= v << 3;
			if (esp_err_t e = i2c_write2(m_bus,m_addr,REG_OSR_CFG,osr))
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


int BMP581::read_data()
{
	uint8_t data[6];
	if (int r = i2c_w1rd(m_bus,m_addr,REG_TEMP_XLSB,data,sizeof(data)))
		return r;
	log_hex(TAG,data,sizeof(data),"data read:");
	int32_t temp = data[0] | (data[1] << 8) | (data[2] << 16);
	if (data[2] & 0x80)
		temp |= 0xff000000;
	m_temp.set((float)temp/65536.0);
	float p = NAN;
	if (m_press_os > 0) {
		uint32_t press = data[3] | (data[4] << 8) | (data[5] << 16);
		log_dbug(TAG,"data: temp %d, press %d",temp,press);
		p = (float)press/64.0;
	}
	m_press.set(p/100);
	return 0;
}


int BMP581::init()
{
	// normal mode : 1
	// 0x1c : 1Hz
	uint8_t cfg = (0x1c << 2) | 1;
	if (esp_err_t r = i2c_write2(m_bus, m_addr, REG_ODR_CFG, cfg))
		log_warn(TAG,"failed to config BMP581@%u,0x%x: %s",m_bus,m_addr,esp_err_to_name(r));
	uint8_t osr = 0x40;	// pressure enable
	if (esp_err_t r = i2c_write2(m_bus, m_addr, REG_OSR_CFG, osr))
		log_warn(TAG,"failed to set OSR BMP581@%u,0x%x: %s",m_bus,m_addr,esp_err_to_name(r));
	m_press_os = 1;
	return 0;
}


unsigned bmp581_scan(uint8_t bus)
{
	unsigned num = 0;
	uint8_t addr = ADDR_MIN;
	do {
		if (0 == I2CDevice::getByAddr(bus,addr)) {
			uint8_t id = 0;
			int r = i2c_w1rd(bus,addr,REG_CHIP_ID,&id,sizeof(id));
			log_dbug(TAG,"scan for bmp581 at 0x%x: id=0x%x",addr,id);
			if ((r >= 0) && (id == CHIP_ID)) {
				uint8_t rev = 0;
				r = i2c_w1rd(bus,addr,REG_CHIP_REV,&rev,sizeof(rev));
				log_dbug(TAG,"BMP581 at 0x%x: %d, id=0x%x",addr,rev);
				new BMP581(bus,addr);
				++num;
			}
		}
		addr += 2;
	} while (addr <= ADDR_MAX);
	return num;
}

#endif	// CONFIG_BMP581
