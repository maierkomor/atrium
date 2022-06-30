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

#ifdef CONFIG_HDC1000

#include "actions.h"
#include "cyclic.h"
#include "event.h"
#include "hdc1000.h"
#include "i2cdrv.h"
#include "log.h"
#include "env.h"

#define REG_TEMP	0x00
#define REG_HUMID	0x01
#define REG_CONFIG	0x02
#define REG_SER01	0xfb
#define REG_SER23	0xfc
#define REG_SER45	0xfd

#define TAG MODULE_HDC1000


HDC1000 *HDC1000::create(uint8_t bus)
{
	uint8_t serial[6];
	if (i2c_w1rd(bus,HDC1000_ADDR,REG_SER01,serial+0,2))
		return 0;
	if (i2c_w1rd(bus,HDC1000_ADDR,REG_SER23,serial+2,2))
		return 0;
	if (i2c_w1rd(bus,HDC1000_ADDR,REG_SER45,serial+4,2))
		return 0;
	uint32_t s0 = serial[0] << 24 | serial[1]<<16 | serial[2]<<8 | serial[3];
	uint16_t s1 = serial[4]<<8 | serial[5];
	log_info(TAG,"serial id %08x%04x",s0,(unsigned)s1);
	return new HDC1000(bus);
}


int HDC1000::init()
{
	m_temp = new EnvNumber("temperature","\u00b0C");
	m_humid = new EnvNumber("humidity","%");
	return setSingle(true);
}


int HDC1000::setSingle(bool single)
{
	// independet measure	[12] = 0=single, 1=both
	// 14 bittemperature:	[10] = 0
	// 14bit humidity:	[9,8] = 00
	m_single = single;
	uint8_t cmd[] =  { HDC1000_ADDR, REG_CONFIG, 0, 0 };
	if (!single)
		cmd[2] |= 1<<4;
	return i2c_write(m_bus,cmd,sizeof(cmd),true,true);
}


unsigned HDC1000::cyclic()
{
	for (;;) {
		switch (m_state) {
		case st_idle:
			if (m_sample == sm_both) {
				log_dbug(TAG,"sample both");
				if (m_single)
					setSingle(false);
				if (i2c_write1(m_bus,HDC1000_ADDR, REG_TEMP))
					break;
				m_sample = sm_none;
				m_state = st_readboth;
				return 15;
			}
			if (m_sample & sm_temp) {
				log_dbug(TAG,"sample temp");
				if (!m_single)
					setSingle(true);
				if (i2c_write1(m_bus,HDC1000_ADDR, REG_TEMP))
					break;
				m_state = st_readtemp;
				m_sample = (sample_t)(m_sample ^ sm_temp);
				return 8;
			}
			if (m_sample & sm_humid) {
				log_dbug(TAG,"sample humid");
				if (!m_single)
					setSingle(true);
				if (i2c_write1(m_bus,HDC1000_ADDR, REG_HUMID))
					break;
				m_state = st_readhumid;
				m_sample = (sample_t)(m_sample ^ sm_humid);
				return 8;
			}
			return 20;
		case st_readhumid:
			{
				log_dbug(TAG,"read humid");
				uint8_t data[2];
				if (i2c_read(m_bus,HDC1000_ADDR,data,sizeof(data)))
					break;
				setHumid(data);
				m_state = st_idle;
				if (m_sample != sm_none)
					continue;
				return 50;
			}
		case st_readtemp:
			{
				log_dbug(TAG,"read temp");
				uint8_t data[2];
				if (i2c_read(m_bus,HDC1000_ADDR,data,sizeof(data)))
					break;
				setTemp(data);
				m_state = st_idle;
				if (m_sample != sm_none)
					continue;
				return 50;
			}
		case st_readboth:
			{
				log_dbug(TAG,"read both");
				uint8_t data[4];
				if (i2c_read(m_bus,HDC1000_ADDR,data,sizeof(data)))
					break;
				setTemp(data);
				setHumid(data+2);
				m_state = st_idle;
				return 50;
			}
		default:
			abort();
		}
		log_dbug(TAG,"error");
		m_state = state_t::st_idle;
		if (m_temp)
			m_temp->set(NAN);
		if (m_humid)
			m_humid->set(NAN);
		return 1000;
	}
}


void HDC1000::setHumid(uint8_t data[])
{
	uint16_t humid_raw = (data[0] << 8) | data[1];
	float h = (float)humid_raw / (1<<16) * 165 - 40;
	m_humid->set(h);
	log_dbug(TAG,"humid %G",h);
}


void HDC1000::setTemp(uint8_t data[])
{
	uint16_t temp_raw = (data[0] << 8) | data[1];
	float t = (float)temp_raw / (1<<16) * 165 - 40;
	log_dbug(TAG,"temp %G",t);
	if (m_temp)
		m_temp->set(t);
}


static unsigned hdc1000_cyclic(void *arg)
{
	HDC1000 *drv = (HDC1000 *) arg;
	return drv->cyclic();
}


void HDC1000::trigger(void *arg)
{
	HDC1000 *drv = (HDC1000 *) arg;
	drv->m_sample = sm_both;
}


void HDC1000::trigger_temp(void *arg)
{
	HDC1000 *drv = (HDC1000 *) arg;
	drv->m_sample = (sample_t)(drv->m_sample | sm_temp);
}


void HDC1000::trigger_humid(void *arg)
{
	HDC1000 *drv = (HDC1000 *) arg;
	drv->m_sample = (sample_t)(drv->m_sample | sm_humid);
}


void HDC1000::attach(EnvObject *root)
{
	root->add(m_temp);
	root->add(m_humid);
	cyclic_add_task(m_name,hdc1000_cyclic,this,0);
	action_add(concat(m_name,"!sample"),HDC1000::trigger,(void*)this,"HDC1000 sample data");
	action_add(concat(m_name,"!temp"),HDC1000::trigger_temp,(void*)this,"HDC1000 sample temperature");
	action_add(concat(m_name,"!humid"),HDC1000::trigger_humid,(void*)this,"HDC1000 sample humidity");
}


#endif
