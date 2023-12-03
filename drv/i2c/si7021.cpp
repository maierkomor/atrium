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

#ifdef CONFIG_SI7021

#include "actions.h"
#include "cyclic.h"
#include "env.h"
#include "log.h"
#include "si7021.h"
#include "terminal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG MODULE_SI7021

#define SI7021_ADDR (0x40 << 1)

#define TIME_RH_8	4
#define TIME_RH_10	5
#define TIME_RH_11	7
#define TIME_RH_12	12
#define TIME_T_11	3
#define TIME_T_12	4
#define TIME_T_13	7
#define TIME_T_14	11

#define TIME_H12_T14	23
#define TIME_H8_T12	8
#define TIME_H10_T13	12
#define TIME_H11_T11	10

static const uint8_t MeasureTime[] = { 23, 8, 12, 10 };

#define CMD_MHUM_HOLD	0xe5
#define CMD_MHUM_NACK	0xf5
#define CMD_MTEM_HOLD	0xe3
#define CMD_MTEM_NACK	0xf3
#define CMD_RD_TEMP	0xe0
#define CMD_RESET	0xfe
#define CMD_WR_UR1	0xe6
#define CMD_RD_UR1	0xe7
#define CMD_WR_HCR	0x51
#define CMD_RD_HCR	0x11
#define CMD_RESET	0xfe

#define VDDS_BIT	(1<<6)
#define HEATER_BIT	(1<<2)



SI7021::SI7021(uint8_t bus, const char *typ, bool combined)
: I2CDevice(bus,SI7021_ADDR,typ)
, m_type(typ)
, m_humid(new EnvNumber("humidity","%","%4.1f"))
, m_temp(new EnvNumber("temperature","\u00b0C","%4.1f"))
, m_combined(combined)
{
	if (i2c_w1rd(m_bus,SI7021_ADDR,CMD_RD_UR1,&m_uc1,sizeof(m_uc1))) {
		log_warn(TAG,"failed to read UR1");
		m_uc1 = 0x3a;
	} else if (m_uc1 & VDDS_BIT) {
		log_warn(TAG,"insufficient power");
	}
}


void SI7021::attach(EnvObject *root)
{
	root->add(m_humid);
	root->add(m_temp);
	if (m_combined) {
		action_add(concat(m_name,"!sample"),triggerh,this,"sample temperature and humidity");
	} else {
		action_add(concat(m_name,"!humid"),triggerh,this,"sample humidity");
		action_add(concat(m_name,"!temp"),triggert,this,"sample temperature");
	}
	cyclic_add_task(m_name,cyclic,this,0);
}


unsigned SI7021::cyclic(void *arg)
{
	uint8_t data[3];
	SI7021 *dev = (SI7021 *) arg;
	unsigned d = 10;
	switch (dev->m_st) {
	case st_off:
		break;
	case st_triggerh:
		{
			uint8_t cmd[] = {SI7021_ADDR, CMD_MHUM_NACK};
			if (0 == i2c_write(dev->m_bus,cmd,sizeof(cmd),0,1)) {
				dev->m_st = st_readh;
				d = MeasureTime[dev->m_mode];
			} else {
				dev->m_st = st_off;
			}
		}
		break;
	case st_triggert:
		{
			uint8_t cmd[] = {SI7021_ADDR, CMD_MTEM_NACK};
			if (0 == i2c_write(dev->m_bus,cmd,sizeof(cmd),0,1)) {
				dev->m_st = st_readt;
				d = MeasureTime[dev->m_mode];
			} else {
				dev->m_st = st_off;
			}
		}
		break;
	case st_readh:
		if (0 == i2c_read(dev->m_bus,SI7021_ADDR,data,sizeof(data))) {
			uint16_t v = (data[0] << 8) | data[1];
			float h = (125.0*v)/65536-6;
			log_dbug(TAG,"humid=%g",h);
			dev->m_humid->set(h);
			if (dev->m_combined && (0 == i2c_w1rd(dev->m_bus,SI7021_ADDR,CMD_RD_TEMP,data,sizeof(data)))) {
				uint16_t v = (data[0] << 8) | data[1];
				float t = (175.72*v)/65536-46.85;
				dev->m_temp->set(t);
				log_dbug(TAG,"temp=%g",t);
			}
			dev->m_st = st_off;
		}
		break;
	case st_readt:
		if (0 == i2c_read(dev->m_bus,SI7021_ADDR,data,sizeof(data))) {
			uint16_t v = (data[0] << 8) | data[1];
			float t = (175.72*v)/65536-46.85;
			dev->m_temp->set(t);
			log_dbug(TAG,"temp=%g",t);
			dev->m_st = st_off;
		}
		break;
	case st_cont:
		break;
	}
	return d;
}


int SI7021::setHeater(bool on)
{
	uint8_t v;
	if (esp_err_t e = i2c_w1rd(m_bus,SI7021_ADDR,CMD_RD_UR1,&v,sizeof(v))) 
		return e;
	if (on)
		v |= HEATER_BIT;
	else
		v &= ~HEATER_BIT;
	uint8_t data[] = { SI7021_ADDR,CMD_WR_UR1,v };
	return i2c_write(m_bus,data,sizeof(data),1,1);
}


int SI7021::reset()
{
	return i2c_write1(m_bus, SI7021_ADDR, CMD_RESET);
}


const char *SI7021::exeCmd(struct Terminal &term, int argc, const char **args)
{
	static const uint8_t tres[] = {12,8,10,11};
	static const uint8_t hres[] = {14,12,13,11};
	if ((argc == 0) || (0 == strcmp(args[0],"-h"))) {
		term.println(
			"res     : to query the set resolutions\n"
			"tres <r>: to set temperature resolution (impacts humidity resolution)\n"
			"hres <r>: to set humidity resolution (impacts temperature resolution)\n"
			"heat    : query heater status\n"
			"heat <o>: turn heater on/off\n"
			"heat <c>: set heater current (if supported)\n"
			);
	} else if (argc == 1) {
		if (0 == strcmp(args[0],"heat")) {
			term.printf("heater is o%s\n",m_uc1 & HEATER_BIT ? "n" : "ff");
			if (m_combined) {
				uint8_t heat;
				if (esp_err_t e = i2c_w1rd(m_bus,SI7021_ADDR,CMD_RD_HCR,&heat,sizeof(heat))) {
					return esp_err_to_name(e);
				}
				float heat_ma = 3.09 + 6.09 * heat;
				term.printf("set to %gmA (o%s)\n",heat_ma,m_uc1&4?"n":"ff");
			}
		} else if (0 == strcmp(args[0],"res")) {
			uint8_t res = ((m_uc1 >> 6) & 0x2) | (m_uc1 & 1);
			term.printf(	"temperature: %ubit\n"
					"humidity   : %ubit\n"
					,tres[res],hres[res]);
		} else {
			return "Invalid argument #1.";
		}
	} else if (argc == 2) {
		if (0 == strcmp(args[0],"heat")) {
			if (0 == strcmp(args[1],"on")) {
				if (esp_err_t e = setHeater(true))
					return esp_err_to_name(e);
				return 0;
			} else if (0 == strcmp(args[1],"off")) {
				if (esp_err_t e = setHeater(false))
					return esp_err_to_name(e);
				return 0;
			} else {
				char *e;
				float f = strtof(args[1],&e);
				if (*e == 'm') {
					f -= 3.09;
					f /= 6.09;
				}
				f = rintf(f);
				if ((f < 0) || (f > 15))
					return "Invalid argument #1.";
				uint8_t v = (uint8_t) f;
				uint8_t data[] = {SI7021_ADDR,CMD_WR_HCR,v};
				if (esp_err_t e = i2c_write(m_bus,data,sizeof(data),1,1)) {
					term.printf("com error: %s\n",esp_err_to_name(e));
					return "";
				}
			}
		} else if (0 == strcmp(args[0],"tres")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || (l < 10) || (l > 12))
				return "Invalid argument #2.";
			int i = 0;
			while (i < sizeof(tres)) {
				if (tres[i] == l)
					break;
				++i;
			}
			uint8_t uc = m_uc1;
			uc &= 0x81;
			if (i & 2)
				uc |= 0x80;
			if (i & 1)
				uc |= 1;
			m_uc1 = uc;
			uint8_t data[] = { SI7021_ADDR, CMD_WR_UR1, uc };
			if (esp_err_t e = i2c_write(m_bus,data,sizeof(data),1,1)) {
				term.printf("com error: %s\n",esp_err_to_name(e));
				return "";
			}
		} else if (0 == strcmp(args[0],"hres")) {
			char *e;
			long l = strtol(args[1],&e,0);
			if (*e || (l < 12) || (l > 14))
				return "Invalid argument #2.";
			int i = 0;
			while (i < sizeof(tres)) {
				if (hres[i] == l)
					break;
				++i;
			}
			uint8_t uc = m_uc1;
			uc &= 0x81;
			if (i & 2)
				uc |= 0x80;
			if (i & 1)
				uc |= 1;
			m_uc1 = uc;
			uint8_t data[] = { SI7021_ADDR, CMD_WR_UR1, uc };
			if (esp_err_t e = i2c_write(m_bus,data,sizeof(data),1,1))
				return esp_err_to_name(e);
		} else {
			return "Invalid argument #1.";
		}
	}
	return 0;
}


void SI7021::triggerh(void *arg)
{
	SI7021 *dev = (SI7021 *) arg;
	if (dev->m_st == st_off)
		dev->m_st = st_triggerh;
	else
		log_dbug(TAG,"cannot trigger while in progress");
}


void SI7021::triggert(void *arg)
{
	SI7021 *dev = (SI7021 *) arg;
	if (dev->m_st == st_off)
		dev->m_st = st_triggert;
	else
		log_dbug(TAG,"cannot trigger while in progress");
}


SI7021 *SI7021::create(uint8_t bus, uint8_t addr)
{
	if (i2c_write1(bus,SI7021_ADDR,CMD_RESET)) {
		log_dbug(TAG,"reset failed");
		return 0;
	}
	vTaskDelay(20);
	uint8_t data[] = { SI7021_ADDR, 0xfa, 0x0f };
	if (esp_err_t e = i2c_write(bus,data,sizeof(data),0,1)) {
		log_dbug(TAG,"no response: %s",esp_err_to_name(e));
		return 0;
	}
	uint8_t id0[8];
	if (i2c_read(bus,SI7021_ADDR,id0,sizeof(id0))) {
		log_dbug(TAG,"read id0 failed");
		return 0;
	}
	log_dbug(TAG,"id0: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",id0[0],id0[1],id0[2],id0[3],id0[4],id0[5],id0[6],id0[7]);
	data[1] = 0xfc;
	data[2] = 0xc9;
	if (i2c_write(bus,data,sizeof(data),0,1)) {
		log_dbug(TAG,"req id1 failed");
		return 0;
	}
	uint8_t id1[6];
	if (i2c_read(bus,SI7021_ADDR,id1,sizeof(id1))) {
		log_dbug(TAG,"read id1 failed");
		return 0;
	}
	log_dbug(TAG,"id1: %02x:%02x:%02x:%02x:%02x:%02x",id1[0],id1[1],id1[2],id1[3],id1[4],id1[5]);
	log_info(TAG,"serial: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",id0[0],id0[2],id0[4],id0[6],id1[0],id1[1],id1[3],id1[4]);
	uint8_t fwver;
	data[1] = 0x84;
	data[2] = 0xb8;
	bool havever = false;
	if (i2c_write(bus,data,sizeof(data),0,1)) {
		log_dbug(TAG,"req fwver failed");
	} else if (i2c_read(bus,SI7021_ADDR,&fwver,1)) {
		log_warn(TAG,"read fwver failed");
	} else {
		havever = true;
	}
	bool combined = true;
	const char *typ = "";
	switch (id1[0]) {
	case 0x0d:
		typ = "si7013";
		break;
	case 0x14:
		typ = "si7020";
		break;
	case 0x00:
	case 0x32:
		typ = "htu21";
		combined = false;
		break;
	default:
		log_warn(TAG,"unknown device id %02x",id1[0]);
		// FALLTHRU
	case 0x15:
		typ = "si7021";
		break;
	}
	if (havever)
		log_info(TAG,"found %s with id 0x%02x%02x, version %x",typ,id0[0],id0[1],fwver);
	else
		log_info(TAG,"found %s",typ);
	return new SI7021(bus,typ,combined);
}

#endif
