/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#ifdef CONFIG_PCA9685

#include "actions.h"
#include "event.h"
#include "pca9685.h"
#include "log.h"

#define TAG MODULE_PCA9685

#if IDF_VERSION >= 50
#define ets_delay_us esp_rom_delay_us
#endif

#define REG_MODE1		0
#define REG_MODE2		1
#define REG_SUBADR1		2
#define REG_SUBADR2		3
#define REG_SUBADR3		4
#define REG_ALLCALLADR		5

#define REG_LED0_ON_L		6
#define REG_LED0_ON_H		7
#define REG_LED0_OFF_L		8
#define REG_LED0_OFF_H		9

#define REG_LEDx_ON_L(x)	((uint8_t)((4*x)+6))
#define REG_LEDx_ON_H(x)	((uint8_t)((4*x)+7))
#define REG_LEDx_OFF_L(x)	((uint8_t)((4*x)+8))
#define REG_LEDx_OFF_H(x)	((uint8_t)((4*x)+9))

#define REG_ALL_LED_ON_L	250
#define REG_ALL_LED_ON_H	251
#define REG_ALL_LED_OFF_L	252
#define REG_ALL_LED_OFF_H	253
#define REG_PRE_SCALE		254
#define REG_TESTMODE		255

// mode1 bits
#define RESTART		(1<<7)
#define EXTCLK		(1<<6)
#define AUTOINC		(1<<5)
#define SLEEP		(1<<4)
#define SUB1		(1<<3)
#define SUB2		(1<<2)
#define SUB3		(1<<1)
#define ALLCALL		(1<<0)

// mode2 bits
#define INVRT		(1<<4)
#define OCH		(1<<3)
#define OUTDRV		(1<<2)
#define OUTNE1		(1<<1)
#define OUTNE0		(1<<0)

static PCA9685 *Instances = 0;


static void set_freq(void *arg)
{
	if (arg == 0) {
		log_warn(TAG,"set: missing argument");
		return;
	}
	const char *txt = (char *)arg;
	log_dbug(TAG,"set %s",txt);
	PCA9685 *dev = Instances;
	if (const char *c = strchr(txt,':')) {
		size_t l = c-txt;
		while (dev && strncmp(txt,dev->getName(),l))
			dev = dev->getNext();
		if (dev == 0) {
			log_warn(TAG,"no such device: %.s",l,txt);
			return;
		}
		txt = c+1;
	}
	char *e;
	long l = strtol(txt,&e,0);
	if (*e) {
		log_warn(TAG,"set prescale - invalid argument %s",txt);
		return;
	}
	dev->setPrescale(l);
}


static void set_values(void *arg)
{
	if (arg == 0) {
		log_warn(TAG,"set: missing argument");
		return;
	}
	const char *txt = (char *)arg;
	log_dbug(TAG,"set %s",txt);
	PCA9685 *dev = Instances;
	if (const char *c = strchr(txt,':')) {
		size_t l = c-txt;
		while (dev && strncmp(txt,dev->getName(),l))
			dev = dev->getNext();
		if (dev == 0) {
			log_warn(TAG,"no such device: %.s",l,txt);
			return;
		}
		txt = c+1;
	}
	unsigned ch,v;
	while (2 == sscanf(txt,"%u=%u",&ch,&v)) {
		dev->setCh(ch,v);
		const char *c = strchr("txt",',');
		if (c == 0)
			return;
		txt = c+1;
	}
}


PCA9685::PCA9685(uint8_t bus, uint8_t addr)
: I2CDevice(bus,addr,drvName())
, m_next(Instances)
{
	action_add(concat(m_name,"!set_prescale"),set_freq,0,"set frequency prescale value");
	action_add(concat(m_name,"!set"),set_values,0,"set PWM channel(s)");
	Instances = this;
}


PCA9685 *PCA9685::create(uint8_t bus, uint8_t addr, bool invrt, bool outdrv, bool xclk)
{
	uint8_t m1;
	addr <<= 1;
	if (i2c_w1rd(bus,addr,REG_MODE1,&m1,sizeof(m1))) {
		log_warn(TAG,"no device at %u,0x%x",bus,addr);
		return 0;
	}
	log_info(TAG,"found device at %u,0x%x",bus,addr);
	m1 |= AUTOINC;
	if (((m1&EXTCLK) != 0) != xclk) {
		if (xclk) {
			if (i2c_write2(bus,addr,REG_MODE1,m1|SLEEP))
				return 0;
			ets_delay_us(1);
			if (i2c_write2(bus,addr,REG_MODE1,m1|EXTCLK))
				return 0;
		} else {
			if (i2c_write2(bus,addr,REG_MODE1,m1|RESTART))
				return 0;
		}
	} else {
		if (i2c_write2(bus,addr,REG_MODE1,m1|RESTART))
			return 0;
	}
	uint8_t m2 = 0;
	m2 |= OCH;	// update on ACK instead of STP
	if (invrt)
		m2 |= INVRT | OUTNE0;
	if (outdrv)
		m2 |= OUTDRV;
	else
		m2 |= OUTNE1;
	if (i2c_write2(bus,addr,REG_MODE2,m2))
		return 0;
	return new PCA9685(bus,addr);
}



void PCA9685::attach(EnvObject *root)
{
	
}


int PCA9685::setCh(int8_t led, uint16_t duty, uint16_t off)
{
	log_dbug(TAG,"setCh %d,%u,%u",led,duty,off);
	if (duty > 4096)
		return -1;
	uint8_t data[] = { m_addr, REG_LEDx_ON_L(led), 0, 0, 0, 0 };
	if (led < 0)
		data[1] = REG_ALL_LED_ON_L;
	if (duty == 0) {
		data[3] = 0x10;
	} else {
		data[2] = off & 0xff;
		data[3] = (uint8_t)(off >> 8);
		data[4] = duty & 0xff;
		data[5] = (uint8_t)(duty >> 8);
	}
	return i2c_write(m_bus,data,sizeof(data),1,1);
}


int PCA9685::setPrescale(uint8_t ps)
{
	if (ps < 3)
		return -1;
	uint8_t m1;
	if (i2c_w1rd(m_bus,m_addr,REG_MODE1,&m1,sizeof(m1)))
		return -1;
	m1 |= SLEEP;
	if (i2c_write2(m_bus,m_addr,REG_MODE1,m1))
		return -1;
	if (i2c_write2(m_bus,m_addr,REG_PRE_SCALE,ps))
		return -1;
	m1 &= ~SLEEP;
	if (i2c_write2(m_bus,m_addr,REG_MODE1,m1))
		return -1;
	return 0;
}


#ifdef CONFIG_I2C_XCMD
const char *PCA9685::exeCmd(Terminal &term, int argc, const char **args)
{
	if (argc == 2) {
		char *e;
		long v = strtol(args[1],&e,0);
		if (0 == strcmp("prescale",args[0])) {
			if (*e || (v < 3))
				return "Invalid argument #1.";
			return setPrescale(v) ? "Failed." : 0;
		}
		if (*e || (v < 0) || (v > 4096))
			return "Value out of range.";
		long l = strtol(args[0],&e,0);
		if (*e || (l < -1) || (l > 15))
			return "Value out of range.";
		return setCh(l,v) ? "Failed." : 0;
	}
	return "Invalid number of arguments.";
}
#endif

#endif // CONFIG_PCA9685
