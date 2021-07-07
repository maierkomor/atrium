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

#ifdef CONFIG_APDS9930

#include "actions.h"
#include "apds9930.h"
#include "cyclic.h"
#include "event.h"
#include "log.h"
#include "ujson.h"

#define REG_ENABLE	0x80
#define REG_ATIME	0x81
#define REG_PTIME	0x82
#define REG_WTIME	0x83
#define REG_AILTL	0x84
#define REG_AILTH	0x85
#define REG_AIHTL	0x86
#define REG_AIHTH	0x87
#define REG_PILTL	0x88
#define REG_PILTH	0x89
#define REG_PIHTL	0x8a
#define REG_PIHTH	0x8b
#define REG_PERS	0x8c
#define REG_CONFIG	0x8d
#define REG_PPULSE	0x8e
#define REG_CTRL	0x8f
#define REG_ID		0x92
#define REG_STATUS	0x93
#define REG_CH0DL	0x94
#define REG_CH0DH	0x95
#define REG_CH1DL	0x96
#define REG_CH1DH	0x97
#define REG_PDL		0x98
#define REG_PDH		0x99
#define REG_POFF	0x9e

#define PSAT	(1<<6)
#define PINT	(1<<5)
#define AINT	(1<<4)
#define PVALID	(1<<1)
#define AVALID	(1<<0)

#define PDIODE		0x20

#define AGAIN_1		(0 << 0)
#define AGAIN_8		(1 << 0)
#define AGAIN_16	(2 << 0)
#define AGAIN_120	(3 << 0)

#define PGAIN_1		(0 << 2)
#define PGAIN_2		(1 << 2)
#define PGAIN_4		(2 << 2)
#define PGAIN_8		(3 << 2)

#define PDRIVE_100	(0 << 6)
#define PDRIVE_50	(1 << 6)
#define PDRIVE_25	(2 << 6)
#define PDRIVE_12_5	(3 << 6)

#define SAI		(1 << 6)	// sleep after interrupt
#define PIEN		(1 << 5)	// proximity interrupt enable
#define AIEN		(1 << 4)	// ALS interrupt enable
#define WEN		(1 << 3)	// wait enable
#define PEN		(1 << 2)	// proximity enable
#define AEN		(1 << 1)	// ALS enable
#define PON		(1 << 0)	// power on

#define ATIME		0xdb
#define ALSIT		(256-ATIME)*2.73

#define AGAIN_V 1

#define CONCAT_M(a,b) a ## b

#define AGAIN_X(V) CONCAT_M(AGAIN_,V)


// configuration section

#define COEF_DF	52.0		// device factor of APDS-9930

// TODO: make configurable
#define COEF_GA	0.49		// attenuation factor of lens (0.49 open air)
#define COEF_B	1.862
#define COEF_C	0.746
#define COEF_D	1.291

#define AGAIN AGAIN_X(AGAIN_V)
#define PGAIN PGAIN_1
#define PDRIVE PDRIVE_100


static const char TAG[] = "apds";


APDS9930::APDS9930(uint8_t port)
: I2CDevice(port,APDS9930_ADDR,drvName())
{ }


int APDS9930::init()
{
	log_dbug(TAG,"init");
	uint8_t cmd[] = { APDS9930_ADDR, 0xa0
		, 0x00		// ENABLE: power off to configure
		, 0xdb		// ATIME: 101ms ALS integration time
		, 0xff		// PTIME: recommended value
		, 0xb6		// WTIME: 202ms @WLONG=0
		, 0xff, 0x00	// AILT
		, 0x00, 0xff	// AIHT
		, 0xff, 0x00	// PILT
		, 0x00, 0xff	// PIHT
		, 0xff		// PERS
		, 0x00		// CONFIG: !AGL | !WLONG | !PDL
		, 0x08		// PPULSE: 8 = recommended value
		, PDRIVE | PDIODE | PGAIN | AGAIN	// CTRL
	};
	m_near = event_register(m_name,"`close");
	m_far = event_register(m_name,"`far");
	return i2c_write(m_bus,cmd,sizeof(cmd),1,1);
}


void APDS9930::attach(JsonObject *root)
{
	log_dbug(TAG,"attach");
	m_lux = new JsonNumber("lux","");
	m_prox = new JsonNumber("prox","");
	root->append(m_lux);
	root->append(m_prox);
	cyclic_add_task(m_name,cycle,this,0);
	action_add(concat(m_name,"!sample"),trigger,(void*)this,"ADPS9930 sample data");
	action_add(concat(m_name,"!poweroff"),trigger,(void*)this,"ADPS9930 sample data");
}


unsigned APDS9930::poweroff()
{
	if (i2c_write2(m_bus,APDS9930_ADDR,REG_ENABLE,0))
		return 0;
	return 500;
}


unsigned APDS9930::read()
{
	uint8_t status;
	if (i2c_w1rd(m_bus,APDS9930_ADDR,REG_STATUS,&status,1))
		return 0;
	if ((status & 3) == 0) {
		log_dbug(TAG,"not ready %x",status);
		if (++m_retry == 3)
			m_state = st_idle;
		return 10;
	}
	m_retry = 0;
	uint8_t data[7];
	if (i2c_w1rd(m_bus,APDS9930_ADDR,REG_CH0DL|0x20,data,sizeof(data)))
		return 0;
	float ch0 = (float)(data[1]<<8|data[0]);
	float ch1 = (float)(data[3]<<8|data[2]);
	float iac1 = ch0 - COEF_B * ch1;
	float iac2 = COEF_C * ch0 - COEF_D * ch1;
	float iac = iac1 < iac2 ? iac2 : iac1;	// max(iac1,iac2,0)
	if (iac < 0)
		iac = 0;
	float lpc = COEF_GA * COEF_DF / (ALSIT * AGAIN_V);
	float lux = lpc*iac;
	m_lux->set(lux);
	unsigned prox = (data[5]<<8) | data[4];
	if (data[6] & 0x80)
		prox <<= data[6]&0x7f;
	else
		prox >>= data[6]&0x7f;
	if (m_close) {
		if (prox < 100) {
			m_close = false;
			event_trigger(m_far);
		}
	} else {
		if (prox > 150) {
			m_close = true;
			event_trigger(m_near);
		}
	}
	m_prox->set(prox);
	log_dbug(TAG,"lux %G, prox %u, iac %G, ch %G/%g",lux,prox,iac,ch0,ch1);
	m_state = st_idle;
	return 20;
}


unsigned APDS9930::cycle(void *arg)
{
	APDS9930 *dev = (APDS9930 *) arg;
	switch (dev->m_state) {
	case st_idle:
		return 50;
	case st_sample:
		log_dbug(TAG,"sample");
		if (i2c_write2(dev->m_bus,APDS9930_ADDR,REG_ENABLE,0xf))
			break;
		dev->m_state = st_read;
		return 12;
	case st_read:
		if (unsigned r = dev->read())
			return r;
		break;
	case st_poweroff:
		if (unsigned r = dev->poweroff())
			return r;
		break;
	default:
		abort();
	}
	log_dbug(TAG,"error");
	dev->m_prox->set(NAN);
	dev->m_lux->set(NAN);
	dev->m_state = st_idle;
	return 1000;
}


/*
void APDS9930::intr_handler(void *arg)
{
	APDS9930 *dev = (APDS9930 *) arg;

}
*/


void APDS9930::trigger(void *arg)
{
	APDS9930 *dev = (APDS9930 *)arg;
	if (dev->m_state == st_idle)
		dev->m_state = st_sample;
}


void APDS9930::poweroff(void *arg)
{
	APDS9930 *dev = (APDS9930 *)arg;
	if (dev->m_state == st_idle)
		dev->m_state = st_poweroff;
}


int apds9930_scan(uint8_t bus)
{
	log_dbug(TAG,"scan");
	int n = 0;
	uint8_t id;
	if (i2c_w1rd(bus,APDS9930_ADDR, REG_ID, &id, 1))
		return 0;
	if (id == 0x39) {
		APDS9930 *drv = new APDS9930(bus);
		drv->init();
		++n;
	}
	return n;
}

#endif
