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

#ifdef CONFIG_CCS811B

#include "actions.h"
#include "ccs811b.h"
#include "cyclic.h"
#include "i2cdrv.h"
#include "log.h"
#include "ujson.h"


#define DEV_ADDR_MIN	(0x5a << 1)
#define DEV_ADDR_MAX	(0x5b << 1)

#define CCS811B_HWID	0x81

#define REG_STATUS	0x00
#define REG_MODE	0x01
#define REG_DATA	0x02
#define REG_RAW_DATA	0x03
#define REG_ENV_DATA	0x05
#define REG_HW_ID	0x20
#define REG_HW_VER	0x21
#define REG_BOOT_VER	0x23
#define REG_APP_VER	0x24
#define REG_ERROR	0xe0
#define REG_START	0xf4

#define STATUS_FLAG_ERROR	0x01
#define STATUS_FLAG_READY	0x08
#define STATUS_APP_VALID	0x10
#define STATUS_APP_VALID	0x10
#define STATUS_DATA_READY	0x08

static const char TAG[] = "ccs811b";


CCS811B::CCS811B(uint8_t bus, uint8_t addr)
: I2CSensor(bus, addr, "ccs811b")
, m_state(st_idle)
{

}


int CCS811B::init()
{
	uint8_t st = status();
	if ((st & STATUS_APP_VALID) != STATUS_APP_VALID) {
		log_warn(TAG,"invalid firmware");
		return 1;
	}
	if (st & 1)
		status();
	if (i2c_write1(m_bus,m_addr,REG_START))
		return 1;
	log_dbug(TAG,"started");
	uint8_t m;
	if (i2c_w1rd(m_bus,m_addr,REG_MODE,&m,sizeof(m)))
		return 1;
	log_dbug(TAG,"mode is 0x%x",(unsigned)m);
	if (m != 0x10) {
		// sample every second
		// no thresholds, no interrupts
		uint8_t cmd[] = { m_addr, REG_MODE, 0x10 };
		if (i2c_write(m_bus,cmd,sizeof(cmd),1,1))
			return 1;
		if (status() & STATUS_FLAG_ERROR) {
			log_warn(TAG,"setup failed, error %x",m_err);
			return 1;
		}
	}
	m_state = st_update;
	log_dbug(TAG,"initialized");
	return 0;
}


void CCS811B::attach(JsonObject *root)
{
	m_root = root;
	m_tvoc = new JsonNumber("tvoc","ppb");
	m_co2 = new JsonNumber("CO2","ppm");
	root->append(m_tvoc);
	root->append(m_co2);
	if (m_state == st_update)
		cyclic_add_task(m_name,cyclic,this,0);
//	action_add(concat(m_name,"!sample"),trigger,(void*)this,"CCS811B sample data");
}


unsigned CCS811B::cyclic(void *arg)
{
	CCS811B *dev = (CCS811B *) arg;
	switch (dev->m_state) {
	case st_idle:
		return 1000;
	case st_update:
		return dev->updateHumidity();
	case st_measure:
		if ((dev->status() & STATUS_FLAG_READY) == 0)
			return 20;
		dev->m_state = st_read;
		/* FALLTHRU */
	case st_read:
		if (unsigned d = dev->read())
			return d;
		break;
	default:
		abort();
	}
	dev->m_tvoc->set(NAN);
	dev->m_co2->set(NAN);
	dev->m_state = st_read;
	return 1000;
}


unsigned CCS811B::updateHumidity()
{
	log_dbug(TAG,"update");
	if (m_humid == 0)
		if (JsonElement *e = m_root->get("humidity"))
			m_humid = e->toNumber();
	if (m_temp == 0)
		if (JsonElement *e = m_root->get("termperature"))
			m_temp = e->toNumber();
	if ((m_humid != 0) && (m_temp != 0)) {
		float t = m_temp->get();
		float h = m_humid->get();
		if (!isnan(t) && !isnan(h)) {
			uint16_t hi = (uint16_t)rint(h * 512);
			uint16_t ti = (uint16_t)rint((t+25) * 512);
			uint8_t cmd[] = { m_addr, REG_ENV_DATA, (uint8_t)(hi >> 8), (uint8_t)(hi & 0xff), (uint8_t)(ti >> 8), (uint8_t)(ti & 0xff) };
			if (i2c_write(m_bus,cmd,sizeof(cmd),true,true))
				log_dbug(TAG,"error updating humidity");
			else if (0 == error())
				log_dbug(TAG,"updated humidity");
		}
	}
	m_state = st_measure;
	return 50;
}


unsigned CCS811B::read()
{
	log_dbug(TAG,"read");
	uint8_t data[6];
	if (i2c_w1rd(m_bus,m_addr,REG_DATA,data,sizeof(data)))
		return 0;
	if ((data[4] & STATUS_FLAG_ERROR) != 0) {
		log_dbug(TAG,"error 0x%x",data[5]);
		return 0;
	} if ((data[4] & STATUS_DATA_READY) == 0)
		return 10;
	unsigned co2 = ((unsigned)data[0] << 8) | data[1];
	unsigned tvoc = ((unsigned)data[2] << 8) | data[3];
	m_co2->set(co2);
	m_tvoc->set(tvoc);
	log_dbug(TAG,"co2=%u, tvoc=%u",co2,tvoc);
	m_state = st_update;
	return 950;
}


uint8_t CCS811B::status()
{
	uint8_t s;
	if (i2c_w1rd(m_bus,m_addr,REG_STATUS,&s,sizeof(s)))
		return STATUS_FLAG_ERROR;
	if (s & STATUS_FLAG_ERROR) {
		if (i2c_w1rd(m_bus,m_addr,REG_ERROR,&m_err,sizeof(m_err)))
			return 0xff;
		log_dbug(TAG,"error %x",(unsigned)m_err);
	} else if (s != 0x10)
		log_dbug(TAG,"status 0x%x",(unsigned)s);
	return s;
}


uint8_t CCS811B::error()
{
	uint8_t err;
	if (i2c_w1rd(m_bus,m_addr,REG_ERROR,&err,sizeof(err)))
		return 0xff;
	log_dbug(TAG,"error %x",(unsigned)err);
	return err;
}


//uint8_t CCS811B::getError()
//{
//
//}


unsigned ccs811b_scan(uint8_t bus)
{
	unsigned n = 0;
	for (uint8_t addr = DEV_ADDR_MIN; addr <= DEV_ADDR_MAX; addr+=2) {
		uint8_t cmd[] = { addr, REG_HW_ID };
		if (i2c_write(bus,cmd,sizeof(cmd),true,true))
			continue;
		uint8_t hwid;
		if (i2c_read(bus,addr,&hwid,1))
			continue;
		if (hwid != CCS811B_HWID)
			continue;
		uint8_t hwver;
		if (i2c_w1rd(bus,addr,REG_HW_VER,&hwver,sizeof(hwver)))
			continue;
		uint8_t bootver[2];
		if (i2c_w1rd(bus,addr,REG_BOOT_VER,bootver,sizeof(bootver)))
			continue;
		uint8_t appver[2];
		if (i2c_w1rd(bus,addr,REG_APP_VER,appver,sizeof(appver)))
			continue;
		log_dbug(TAG,"versions: hw %x, boot %x, app %x",hwver,((uint16_t)bootver[0]<<8)|bootver[1],appver[0]<<8|appver[1]);
		if ((hwver & 0xf0) != 0x10)
			log_warn(TAG,"unexpected hardware version");
		CCS811B *dev = new CCS811B(bus,addr);
		dev->init();
		++n;
	}
	return n;
}

#endif
