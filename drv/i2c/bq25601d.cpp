/*
 *  Copyright (C) 2023, Thomas Maier-Komor
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

#ifdef CONFIG_BQ25601D

#include "actions.h"
#include "bq25601d.h"
#include "cyclic.h"
#include "env.h"
#include "event.h"
#include "i2cdrv.h"
#include "log.h"
#include "stream.h"
#include "terminal.h"
#include "xio.h"

#define TAG MODULE_BQ25

#define DEV_ADDR (0x6b<<1)
#define DEV_ID 0x2
#define NUM_REGS 12


static const char *topoff_str[] = { "disabled", "15min", "30min", "45min" };
static const char *charge_str[] = { "off", "pre", "fast", "term" };
static const char *chargeflt_str[] = { "ok", "input-fault", "thermal shutdown", "safety timeout" };
static const char *vbus_str[] = { "none", "SDP/500mA", "CDP/1.5A", "DCP/2.4A", "500mA", "other", "OTG" };
static const char *jeita_str[] = { "normal", "undef1", "warm", "cool", "undef4", "cold", "hot", "undef7" };
static uint8_t SysMin[] = {26,28,30,32,34,35,36,37};	// deciVolt

BQ25601D *Bq25Instance = 0;


BQ25601D::BQ25601D(uint8_t port, uint8_t addr, const char *n)
: I2CDevice(port,addr,n ? n : drvName())
, m_pg("power-good",false)
, m_charge("charge","")
, m_vbus("Vbus","")
, m_onev(event_register(m_name,"`buson"))
, m_offev(event_register(m_name,"`busoff"))
{
	Bq25Instance = this;
	action_add(concat(m_name,"!off"),powerDown,this,"enter HiZ mode");
}


void BQ25601D::addIntr(uint8_t intr)
{
	/*
	m_irqev = event_register(m_name,"`irq");
	log_info(TAG,"irqev %d",m_irqev);
	Action *a = action_add(concat(m_name,"!isr"),processIntr,this,0);
	event_callback(m_irqev,a);
	xio_cfg_t cfg = XIOCFG_INIT;
	cfg.cfg_io = xio_cfg_io_in;
	cfg.cfg_pull = xio_cfg_pull_none;
	cfg.cfg_intr = xio_cfg_intr_fall;
	if (0 > xio_config(intr,cfg)) {
		log_warn(TAG,"config interrupt error");
	} else if (esp_err_t e = xio_set_intr(intr,intrHandler,this)) {
		log_warn(TAG,"error attaching interrupt: %s",esp_err_to_name(e));
	} else {
		log_info(TAG,"BQ25601D@%u,0x%x: interrupt on GPIO%u",m_bus,m_addr,intr);
	}
	*/
}


void BQ25601D::attach(EnvObject *root)
{
	uint8_t regs[3];
	if (0 == i2c_w1rd(m_bus,DEV_ADDR,0x8,regs,sizeof(regs))) {
		m_vbus.set(vbus_str[regs[0]>>5]);
		m_pg.set(regs[0]&0x4);
		if (uint8_t flt = (regs[1]>>4)&0x3)
			m_charge.set(chargeflt_str[flt]);
		else
			m_charge.set(charge_str[(regs[0]>>3)&0x3]);
	}
	cyclic_add_task(m_name,cyclic,this,0);
	root->add(&m_vbus);
	root->add(&m_charge);
	root->add(&m_pg);
}


unsigned BQ25601D::cyclic(void *arg)
{
	BQ25601D *dev = (BQ25601D *)arg;
	return dev->cyclic();
}


unsigned BQ25601D::cyclic()
{
	uint8_t regs[3];
	if (esp_err_t e = i2c_w1rd(m_bus,DEV_ADDR,0x8,regs,sizeof(regs))) {
		log_warn(TAG,"i2c error: %s",esp_err_to_name(e));
		return 1000;
	}
//	log_hex(TAG,regs,sizeof(regs),"cyclic");
	if (uint8_t reg08c = regs[0] ^ m_regs[0x8]) {
		log_dbug(TAG,"reg08 changed: 0x%02x",regs[0]);
		if (reg08c & 0xe0) {
			log_dbug(TAG,"vbus status: %s",vbus_str[regs[0]>>5]);
			event_trigger(((regs[0]>>5) != 0) ? m_onev : m_offev);
			m_vbus.set(vbus_str[regs[0]>>5]);
		}
		if (reg08c & 0x18) {
			const char *crg = charge_str[(regs[0]>>3)&0x3];
			log_dbug(TAG,"charge status: %s",crg);
			m_charge.set(crg);
		}
		if (reg08c & 0x4) {
			bool pg = regs[0]&0x4;
			log_dbug(TAG,"power %s",pg ? "good" : "fault");
			m_pg.set(pg);
		}
		if (reg08c & 0x2)
			log_dbug(TAG,"thermal regulation %sactive",regs[0]&0x2 ? "" : "in");
		if (reg08c & 0x1)
			log_dbug(TAG,"Vsysmin regulation %sactive",regs[0]&0x1 ? "" : "in");
		m_regs[0x8] = regs[0];
	}
	if (uint8_t reg09c = regs[1] ^ m_regs[0x9]) {
		log_dbug(TAG,"reg09 changed: 0x%02x",regs[1]);
		if (reg09c & 0x80)
			log_dbug(TAG,"watchdog %s",regs[1]&0x80?"expired":"ok");
		if (reg09c & 0x40)
			log_dbug(TAG,"boost %s",regs[1]&0x40?"fault":"ok");
		if (reg09c & 0x30) {
			uint8_t flt = (regs[1]>>4)&0x3;
			log_dbug(TAG,"charge %s",chargeflt_str[flt]);
			if (flt)
				m_charge.set(chargeflt_str[flt]);
		}
		if (reg09c & 0x8)
			log_dbug(TAG,"BAT %s",regs[1]&0x8?"OVP":"ok");
		if (reg09c & 0x7)
			log_dbug(TAG,"JEITA %s",jeita_str[regs[1]&0x7]);
		m_regs[0x9] = regs[1];
	}
	if (uint8_t reg0ac = regs[2] ^ m_regs[0xa]) {
		log_dbug(TAG,"reg0a changed: 0x%02x",regs[2]);
		if (reg0ac & 0x80)
			log_dbug(TAG,"Vbus %s",regs[2]&0x80?"attached":"off");
		if (reg0ac & 0x40)
			log_dbug(TAG,"VinDPM %s",regs[2]&0x40?"active":"off");
		if (reg0ac & 0x20)
			log_dbug(TAG,"IinDPM %s",regs[2]&0x20?"active":"off");
		if (reg0ac & 0x8)
			log_dbug(TAG,"top-off %s",regs[2]&0x8?"counting":"expired");
		m_regs[0xa] = regs[2];
	}
	return 100;
}


void BQ25601D::powerDown(void *arg)
{
	BQ25601D *dev = (BQ25601D *) arg;
	uint8_t reg00;
	if (i2c_w1rd(dev->m_bus,DEV_ADDR,0x00,&reg00,sizeof(reg00)))
		log_warn(TAG,"I2C error");
	reg00 |= (1<<7);
	if (i2c_write2(dev->m_bus,DEV_ADDR,0x00,reg00))
		log_warn(TAG,"I2C error");
	uint8_t reg07;
	if (i2c_w1rd(dev->m_bus,DEV_ADDR,0x07,&reg07,sizeof(reg07))) {
		log_warn(TAG,"I2C error");
	} else {
		reg07 |= (1<<5);
		if (i2c_write2(dev->m_bus,DEV_ADDR,0x07,reg07))
			log_warn(TAG,"I2C error");
	}
}


#ifdef CONFIG_I2C_XCMD
const char *BQ25601D::exeCmd(Terminal &term, int argc, const char **args)
{
	if (argc == 0) {
		if (i2c_w1rd(m_bus,DEV_ADDR,0x0,m_regs,sizeof(m_regs)))
			return "I2C error.";
		uint8_t reg08 = m_regs[8];
		term.printf(
			"VBus: %s\n"
			"HiZ: %s\n"
			"charging: %s\n"
			"power: %s\n"
			"thermal: %s regulation\n"
			"Vbat %c Vsys\n"
			, vbus_str[reg08>>5]
			, m_regs[0] & 0x80 ? "on" : "off"
			, charge_str[(reg08 >> 2) & 3]
			, reg08 & 4 ? "good" : "unstable"
			, reg08 & 2 ? "in" : "no"
			, reg08 & 1 ? '>' : '<'
		);
		uint8_t reg09 = m_regs[9];
		term.printf(
			"WD %s, VBUS %s, charge %s, BAT %s, JEITA %s\n"
			, reg09 & 0x80 ? "expired" : "OK"
			, reg09 & 0x40 ? "fault" : "OK"
			, chargeflt_str[(reg09>>4) & 0x3]
			, reg09 & 0x8 ? "ok" : "OVP"
			, jeita_str[reg09 & 0x7]
		);
		uint8_t reg0a = m_regs[0xa];
		term.printf(
			"VBus %sattached\n"
			"%sin VINDPM\n"
			"%sin IINDPM\n"
			"top-off %sactive\n"
			"%sin ACOV\n"
			, reg0a&0x80 ?"":"not "
			, reg0a&0x40 ?"":"not "
			, reg0a&0x20 ?"":"not "
			, reg0a&0x8  ?"":"not "
			, reg0a&0x4  ?"":"not "
			);
		term.printf("Ipre  %4umA\n",(m_regs[3]>>4)*60+60);
		term.printf("Ichg  %4umA\n",(m_regs[2]&0x3f)*60);
		term.printf("Iterm %4umA\n",(m_regs[3]&0xf)*60+60);
		term.printf("Imax  %4umA\n",(m_regs[0]&0x1f)*100);
	} else {
		if (0 == strcmp(args[0],"-h")) {
			term.println(
				"hiz [<on/off>]    : hi-Z mode\n"
				"pfm [<on/off>]    : PFM mode\n"
				"q1on [<on/off>]   : Q1 full-on mode\n"
				"batfet [<on/off>] : BATFET mode\n"
				"vreg [<mV>]       : Vreg voltage in mV\n"
				"boostv [<mV>]     : boost voltage in mV\n"
				"vsysmin [<mV>]    : minimum system voltage in mV\n"
				"iterm [<mA>]      : termination current in mA\n"
				"ipre [<mA>]       : pre-charge current in mA\n"
				"ichg [<mA>]       : charge current in mA\n"
				);
		} else if (0 == strcmp(args[0],"vreg")) {
			uint8_t reg04;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x04,&reg04,sizeof(reg04)))
				return "I2C error.";
			if (argc == 1) {
				unsigned vreg = 3847;
				vreg += (reg04 & 0xf8) << 2;
				unsigned topoff = (reg04 >> 1) & 3;
				term.printf("vreg %umV, topoff %s, recharge %u00mV\n",vreg,topoff_str[topoff],(reg04&1)+1);
			} else {
				char *e;
				long l = strtol(args[1],&e,0);
				if (*e || (l < 3847) || (l > 4650))
					return "Invalid argument #1.";
				l -= 3847;
				l >>= 2;
				l &= 0xf1;
				l |= reg04 & 7;
				if (i2c_write2(m_bus,DEV_ADDR,0x04,(uint8_t)l))
					return "I2C error.";
				m_regs[0x04] = reg04;
			}
		} else if (0 == strcmp(args[0],"batfet")) {
			uint8_t reg07;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x07,&reg07,sizeof(reg07)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("batfet %sabled\n",(reg07>>5)&1 ? "dis" : "en");
			} else {
				bool b;
				if (arg_bool(args[1],&b))
					return "Invalid argument #2.";
				if (b)
					reg07 &= ~(1<<5);
				else
					reg07 |= (1<<5);
				if (i2c_write2(m_bus,DEV_ADDR,0x07,reg07))
					return "I2C error.";
				m_regs[0x07] = reg07;
			}
		} else if (0 == strcmp(args[0],"hiz")) {
			uint8_t reg00;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x00,&reg00,sizeof(reg00)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("HiZ %sabled\n",(reg00>>7)&1 ? "en" : "dis");
			} else {
				bool b;
				if (arg_bool(args[1],&b))
					return "Invalid argument #2.";
				if (b)
					reg00 |= (1<<7);
				else
					reg00 &= ~(1<<7);
				if (i2c_write2(m_bus,DEV_ADDR,0x00,reg00))
					return "I2C error.";
				m_regs[0x0] = reg00;
			}
		} else if (0 == strcmp(args[0],"pfm")) {
			uint8_t reg01;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x01,&reg01,sizeof(reg01)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("PFM %sabled\n",(reg01>>7)&1 ? "dis" : "en");
			} else {
				bool b;
				if (arg_bool(args[1],&b))
					return "Invalid argument #2.";
				if (b)
					reg01 &= ~(1<<7);
				else
					reg01 |= (1<<7);
				if (i2c_write2(m_bus,DEV_ADDR,0x01,reg01))
					return "I2C error.";
				m_regs[0x01] = reg01;
			}
		} else if (0 == strcmp(args[0],"boostv")) {
			uint8_t reg06;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x06,&reg06,sizeof(reg06)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("boostv %umV\n",((reg06>>4)&3)*150+4850);
			} else {
				char *e;
				long l = strtol(args[1],&e,0);
				if (*e || (l < 4850) || (l > 5300))
					return "Invalid argument #2.";
				l -= 4850;
				l /= 150;
				reg06 &= 0xcf;
				reg06 |= (l & 0x3) << 4;
//				term.printf("reg06: 0x%x\n",reg06);
				if (i2c_write2(m_bus,DEV_ADDR,0x06,reg06))
					return "I2C error.";
				m_regs[0x06] = reg06;
			}
		} else if (0 == strcmp(args[0],"imax")) {
			uint8_t reg00;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x00,&reg00,sizeof(reg00)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("Imax %umA\n",(reg00&0x1f)*100);
			} else {
				char *e;
				long l = strtol(args[1],&e,0);
				if (*e || (l < 100) || (l > 3200))
					return "Invalid argument #2.";
				l /= 100;
				reg00 &= 0xe0;
				reg00 |= l & 0x1f;
				if (i2c_write2(m_bus,DEV_ADDR,0x00,reg00))
					return "I2C error.";
				m_regs[0x0] = reg00;
			}
		} else if (0 == strcmp(args[0],"iterm")) {
			uint8_t reg03;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x03,&reg03,sizeof(reg03)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("Iterm %umA\n",(reg03&0xf)*60+60);
			} else {
				char *e;
				long l = strtol(args[1],&e,0);
				if (*e || (l < 60) || (l > 960))
					return "Invalid argument #2.";
				l -= 60;
				l /= 60;
				reg03 &= 0xf0;
				reg03 |= l & 0xf;
				if (i2c_write2(m_bus,DEV_ADDR,0x03,reg03))
					return "I2C error.";
				m_regs[0x3] = reg03;
			}
		} else if (0 == strcmp(args[0],"ipre")) {
			uint8_t reg03;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x03,&reg03,sizeof(reg03)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("Ipre %umA\n",(reg03>>4)*60+60);
			} else {
				char *e;
				long l = strtol(args[1],&e,0);
				if (*e || (l < 60) || (l > 780))
					return "Invalid argument #2.";
				l -= 60;
				l /= 60;
				reg03 &= 0xf;
				reg03 |= (l & 0xf)<<4;
				if (i2c_write2(m_bus,DEV_ADDR,0x03,reg03))
					return "I2C error.";
				m_regs[0x3] = reg03;
			}
		} else if (0 == strcmp(args[0],"ichg")) {
			uint8_t reg02;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x02,&reg02,sizeof(reg02)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("Ichg %umA\n",(reg02&0x3f)*60);
			} else {
				char *e;
				long l = strtol(args[1],&e,0);
				if (*e || (l < 0) || (l > 3000))
					return "Invalid argument #2.";
				l /= 60;
				reg02 &= 0xc0;
				reg02 |= l & 0x3f;
				if (i2c_write2(m_bus,DEV_ADDR,0x02,reg02))
					return "I2C error.";
				m_regs[0x2] = reg02;
			}
		} else if (0 == strcmp(args[0],"vsysmin")) {
			uint8_t reg01;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x01,&reg01,sizeof(reg01)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("Vsysmin %umV\n",SysMin[reg01>>1&0x7]*100);
			} else {
				char *e;
				long l = strtol(args[1],&e,0);
				if (*e || (l < 2600) || (l > 3700))
					return "Invalid argument #2.";
				l /= 100;
				uint8_t v = 0;
				while (SysMin[v] < l)
					++v;
				reg01 &= 0xf1;
				reg01 |= v << 1;
				if (i2c_write2(m_bus,DEV_ADDR,0x01,reg01))
					return "I2C error.";
				m_regs[0x1] = reg01;
			}
		} else if (0 == strcmp(args[0],"vindpm")) {
			uint8_t reg06;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x06,&reg06,sizeof(reg06)))
				return "I2C error.";
			term.printf("vindpm %umV\n",((reg06)&0xf)*100+3900);
		} else if (0 == strcmp(args[0],"q1on")) {
			// Q1On is only relevant when boost is disabled
			uint8_t reg02;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x02,&reg02,sizeof(reg02)))
				return "I2C error.";
			if (argc == 1) {
				term.printf("Q1 %s\n",reg02 & 0x40 ? "fullon" : "auto");
			} else {
				bool on;
				if (arg_bool(args[1],&on))
					return "Invalid argument #2.";
				if (on)
					reg02 |= 0x40;
				else
					reg02 &= ~0x40;
				if (i2c_write2(m_bus,DEV_ADDR,0x02,reg02))
					return "I2C error.";
				m_regs[0x2] = reg02;
			}
		} else if (0 == strcmp(args[0],"regs")) {
			if (i2c_w1rd(m_bus,DEV_ADDR,0x00,m_regs,sizeof(m_regs)))
				return "I2C error.";
			term.print_hex(m_regs,sizeof(m_regs));
		/*
		} else if (0 == strcmp(args[0],"topoff")) {
			uint8_t reg04;
			if (i2c_w1rd(m_bus,DEV_ADDR,0x04,&reg04,sizeof(reg04)))
				return "I2C error.";
			if (argc == 1) {
				unsigned topoff = (reg04 >> 1) & 3;
				term.printf("topoff %s\n",topoff_str[topoff]);
			}
		*/
		} else {
			return "Invalid argument #1.";
		}
	}
	return 0;
}
#endif


void IRAM_ATTR BQ25601D::intrHandler(void *arg)
{
	BQ25601D *drv = (BQ25601D *) arg;
	if (drv != 0) {
		++drv->m_irqcnt;
		event_isr_trigger(drv->m_irqev);
	}
}


inline void BQ25601D::processIntr()
{
	// read status registers 0x8..0xa
	uint8_t oldreg09, newreg09;
	if (esp_err_t e = i2c_w1rd(m_bus,DEV_ADDR,0x9,&oldreg09,sizeof(oldreg09))) {
		log_warn(TAG,"i2c error: %s",esp_err_to_name(e));
		return;
	}
	if (oldreg09 != m_regs[0x9])
		log_warn(TAG,"missed flags update");
	if (esp_err_t e = i2c_w1rd(m_bus,DEV_ADDR,0x9,&newreg09,sizeof(newreg09))) {
		log_warn(TAG,"i2c error: %s",esp_err_to_name(e));
		return;
	}
	uint8_t regs[3];
	if (esp_err_t e = i2c_w1rd(m_bus,DEV_ADDR,0x8,regs,sizeof(regs))) {
		log_warn(TAG,"i2c error: %s",esp_err_to_name(e));
		return;
	}
	if (newreg09 != m_regs[0x9])
		log_warn(TAG,"unexpected flags update");
	log_hex(TAG,regs,sizeof(regs),"interrupt");
	log_hex(TAG,m_regs+8,sizeof(regs),"old");
	if (uint8_t reg08c = regs[0] ^ m_regs[0x8]) {
		if (reg08c & 0xe0) {
			log_info(TAG,"vbus status: %s",vbus_str[regs[0]>>5]);
			event_trigger((regs[0]>>5) != 0 ? m_onev : m_offev);
		}
		if (reg08c & 0x18)
			log_info(TAG,"charge status: %s",charge_str[(regs[0]>>3)&0x3]);
		if (reg08c & 0x4)
			log_info(TAG,"power %s",regs[0]&0x4 ? "good" : "fault");
		if (reg08c & 0x2)
			log_info(TAG,"thermal regulation %sactive",regs[0]&0x2 ? "" : "in");
		if (reg08c & 0x1)
			log_info(TAG,"Vsysmin regulation %sactive",regs[0]&0x1 ? "" : "in");
		m_regs[0x8] = regs[0];
	}
	if (uint8_t reg09c = regs[1] ^ m_regs[0x9]) {
		if (reg09c & 0x80)
			log_info(TAG,"watchdog %s",regs[1]&0x80?"expired":"ok");
		if (reg09c & 0x40)
			log_info(TAG,"boost %s",regs[1]&0x40?"fault":"ok");
		if (reg09c & 0x30)
			log_info(TAG,"charge %s",chargeflt_str[(regs[1]>>4)&0x3]);
		if (reg09c & 0x8)
			log_info(TAG,"BAT %s",regs[1]&0x8?"OVP":"ok");
		if (reg09c & 0x7)
			log_info(TAG,"JEITA %s",jeita_str[regs[1]&0x7]);
		m_regs[0x9] = regs[1];
	}
	if (uint8_t reg0ac = regs[2] ^ m_regs[0xa]) {
		if (reg0ac & 0x80)
			log_info(TAG,"Vbus %s",regs[2]&0x80?"attached":"off");
		if (reg0ac & 0x40)
			log_info(TAG,"VinDPM %s",regs[2]&0x40?"active":"off");
		if (reg0ac & 0x20)
			log_info(TAG,"IinDPM %s",regs[2]&0x20?"active":"off");
		if (reg0ac & 0x8)
			log_info(TAG,"top-off %s",regs[2]&0x8?"counting":"expired");
		m_regs[0xa] = regs[2];
	}
}


void BQ25601D::processIntr(void *arg)
{
	BQ25601D *dev = (BQ25601D *) arg;
	dev->processIntr();
}


BQ25601D *BQ25601D::scan(uint8_t bus)
{
	log_dbug(TAG,"scan bq25601d");
	uint8_t data[NUM_REGS];
	if (esp_err_t e = i2c_w1rd(bus,DEV_ADDR,0x0,data,sizeof(data))) {
		log_dbug(TAG,"device scan failed: %s",esp_err_to_name(e));
		return 0;
	}

	uint8_t reg0b = data[11];
	log_hex(TAG,data,sizeof(data),"registers");
	uint8_t id = (reg0b >> 3) & 0xf;
	if (id != DEV_ID) {
		log_dbug(TAG,"unsupported device id 0x%x",id);
		return 0;
	}
	log_dbug(TAG,"revision %d",id&3);
	return new BQ25601D(bus,DEV_ADDR);
}


int BQ25601D::setImax(unsigned imax)
{
	/*	reg02 Ichg
	uint8_t reg02 = m_regs[0x2];
	if (imax > 3000)
		imax = 3000;
	imax /= 60;
	reg02 &= 0xc0;
	reg02 |= imax & 0x3f;
	if (i2c_write2(m_bus,DEV_ADDR,0x02,reg02))
		return -1;
	m_regs[0x2] = reg02;
	return (reg02&0x3f)*60;
	*/
	uint8_t reg00 = m_regs[0];
	if (imax > 3200)
		imax = 3200;
	imax /= 100;
	reg00 &= 0xe0;
	reg00 |= imax & 0x1f;
	if (i2c_write2(m_bus,DEV_ADDR,0x00,reg00))
		return -1;
	return imax * 100;
}


int BQ25601D::getImax()
{
	uint8_t reg00;
	if (i2c_w1rd(m_bus,DEV_ADDR,0x02,&reg00,sizeof(reg00)))
		return -1;
	m_regs[0] = reg00;
	return (reg00&0x1f)*100;
}

#endif
