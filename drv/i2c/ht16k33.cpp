/*
 *  Copyright (C) 2021-2025, Thomas Maier-Komor
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

#if defined CONFIG_HT16K33

#include "actions.h"
#include "event.h"
#include "ht16k33.h"
#include "i2cdrv.h"
#include "log.h"
#include "terminal.h"

#include <string.h>


#define CMD_SYSTEM	0x20
#define CMD_SYS_OFF	0x20
#define CMD_SYS_ON	0x21
#define CMD_WDATA	0x00

#define CMD_DISP	0x80
#define CMD_DISP_ON	0x81
#define CMD_DISP_2HZ	0x83
#define CMD_DISP_1HZ	0x85
#define CMD_DISP_05HZ	0x87
#define CMD_RDINTR	0x60
#define CMD_ROWINT	0xa0

#define CMD_DIM		0xe0

#define DEV_ADDR_MIN	(0x70<<1)
#define DEV_ADDR_MAX	(0x77<<1)

#define BIT_ON		0x1
#define BITS_BLINK	0x6

#define DIM_MAX		15
#define BLINK_MAX	3

#define TAG MODULE_HT16K33


typedef enum { disp_off, disp_on, blink_05hz, blink_1hz, blink_2hz } disp_t;


void HT16K33::create(uint8_t bus, uint8_t addr)
{
	if (addr == 0) {
		for (uint8_t a = DEV_ADDR_MIN; a <= DEV_ADDR_MAX; a += 2)
			create(bus,a);
	} else if ((addr < DEV_ADDR_MIN) || (addr > DEV_ADDR_MAX)) {
		log_warn(TAG,"invalid address 0x%x",(unsigned)addr);
	} else {
		log_dbug(TAG,"power up");
		if (i2c_write1(bus,addr,CMD_SYS_ON))
			log_warn(TAG,"HT16k33 at %u,0x%x did not answer",bus,(unsigned)addr);
		if (HT16K33 *dev = new HT16K33(bus,addr))
			dev->init();
	}
}


static void ht16k33_on(void *arg)
{
	if (arg) {
		HT16K33 *dev = (HT16K33 *) arg;
		dev->setDisplay(true);
	}
}


static void ht16k33_off(void *arg)
{
	if (arg) {
		HT16K33 *dev = (HT16K33 *) arg;
		dev->setDisplay(false);
	}
}


static void ht16k33_power(void *arg)
{
	if (arg) {
		HT16K33 *dev = (HT16K33 *) arg;
		dev->setPower(true);
	}
}


static void ht16k33_standby(void *arg)
{
	if (arg) {
		HT16K33 *dev = (HT16K33 *) arg;
		dev->setPower(false);
	}
}


int HT16K33::setPower(bool on)
{
	int r = i2c_write1(m_bus,m_addr,CMD_SYSTEM|on);
	if (r)
		log_warn(TAG,"power %d: %d",on,r);
	else
		log_dbug(TAG,"power %d",on);
	return r;
}


int HT16K33::cfgIntr(bool on, bool act_high)
{
	int r = i2c_write1(m_bus,m_addr,CMD_ROWINT|on|(act_high<<1));
	if (r)
		log_warn(TAG,"row/int %s: %d",on?(act_high?"high":"low"):"row",r);
	else
		log_dbug(TAG,"row/int %s",on?(act_high?"high":"low"):"row");
	return r;
}


int HT16K33::init()
{
	log_info(TAG,"initializing device at 0x%x",(unsigned)m_addr>>1);
	//setDisplay(false);
	cfgIntr(false);
	setDim(0xf);
	setBlink(0);
	clear();
	// power/standby needs additional testing
	// resume from standby needs reinitialization
//	action_add(concat(getName(),"!power"),ht16k33_power,this,"power device on");
//	action_add(concat(getName(),"!standby"),ht16k33_standby,this,"put device in standby mode");
	action_add(concat(getName(),"!on"),ht16k33_on,this,"turn display on");
	action_add(concat(getName(),"!off"),ht16k33_off,this,"turn display off");
	return 0;
}


const char *HT16K33::drvName() const
{
	return "ht16k33";
}


int HT16K33::setOffset(unsigned pos)
{
	log_dbug(TAG,"setOff(%u)",pos);
	if (pos >= m_digits)
		return 1;;
	m_pos = pos;
	return 0;
}


int HT16K33::clear()
{
	uint8_t data[18] = { 0 };
	data[0] = m_addr;
	int r = i2c_write(m_bus,data,sizeof(data),true,true);
	log_dbug(TAG,"clear: %d",r);
	m_pos = 0;
	bzero(m_data,sizeof(m_data));
	return 0;
}


int HT16K33::setPos(uint8_t x, uint8_t y)
{
	m_pos = x;
	if ((x >= m_digits) || (y != 0))
		return 1;
	return 0;
}


int HT16K33::write(uint8_t v)
{
	if ((v == '\r') || (v == '\n')) {
		m_pos = 0;
		return 0;
	}
	if (m_pos >= m_digits)
		return 1;
	uint8_t off = m_pos;
	if (m_data[off] != v) {
		uint8_t data[] = { m_addr, (uint8_t)off, v };
		int r = i2c_write(m_bus,data,sizeof(data),true,true);
		log_dbug(TAG,"write 0x%x: %d",v,r);
		if (r)
			return -1;
		m_data[off] = v;
	}
	++off;
	m_pos = off;
	return 0;
}


int HT16K33::write(uint8_t *v, unsigned n)
{
	if (m_pos >= m_digits)
		return 1;
	uint8_t off = m_pos;
	if (n + off >= m_digits)
		n = m_digits - off;
	if (0 != memcmp(m_data+off,v,n)) {
		uint8_t data[] = { m_addr , (uint8_t)off };
		int r = i2c_write(m_bus,data,sizeof(data),false,true);
		log_hex(TAG,v,n,"write %d",r);
		if (r)
			return -1;
		r = i2c_write(m_bus,v,n,true,false);
		log_dbug(TAG,"write %d",r);
		if (r)
			return -1;
		memcpy(m_data+off,v,n);
	}
	off += n;
	m_pos = off;
	return 0;
}


int HT16K33::setDisplay(bool on)
{
	if (on == (m_disp&BIT_ON))
		return 0;
	int r = i2c_write1(m_bus,m_addr,CMD_DISP|on);
	if (0 == r) {
		m_disp &= ~BIT_ON;
		m_disp |= on;
	}
	log_dbug(TAG,"set on %d: %d",on,r);
	return r;
}


int HT16K33::setBlink(uint8_t blink)
{
	if (blink > BLINK_MAX)
		return -1;
	uint8_t v = m_disp & ~BITS_BLINK;
	v |= (blink<<1);
	int r = i2c_write1(m_bus,m_addr,CMD_DISP|v);
	if (0 == r)
		m_disp = v;
	log_dbug(TAG,"set blink %d: %d",blink,r);
	return r;
}


int HT16K33::setNumDigits(unsigned n)
{
	if (n <= sizeof(m_data)) {
		m_digits = n;
		return 0;
	}
	return -1;
}


int HT16K33::setDim(uint8_t dim)
{
	int r;
	if ((dim & 0xf0) == 0) {
		r = i2c_write1(m_bus,m_addr,CMD_DIM | dim);
		log_dbug(TAG,"set dim %d: %d",dim,r);
	} else {
		log_dbug(TAG,"dim out of range");
		r = -1;
	}
	return r;
}


#ifdef CONFIG_I2C_XCMD
const char *HT16K33::exeCmd(Terminal &term, int argc, const char **args)
{
	if ((argc == 0) || ((argc == 1) && (0 == strcmp(args[0],"-h")))) {
		term.println(
			"on       : display on\n"
			"off      : display off\n"
			"dim <d>  : set dim, range 0..15\n"
			"blink <b>: range 0..3\n"
			);
		return 0;
	}
	if (argc == 1) {
		if (0 == strcmp(args[0],"on")) {
			setDisplay(true);
		} else if (0 == strcmp(args[0],"off")) {
			setDisplay(false);
		} else {
			return "Invalid argument #2.";
		}
	} else if (2 == argc) {
		char *e;
		long v = strtol(args[1],&e,0);
		if (*e)
			return "Invalid argument #3.";
		if (0 == strcmp(args[0],"dim")) {
			if ((v < 0) || (v > DIM_MAX))
				return "Invalid argument #3.";
			setDim(v);
		} else if (0 == strcmp(args[0],"blink")) {
			if ((v < 0) || (v > BLINK_MAX))
				return "Invalid argument #3.";
			setBlink(v);
		} else {
			return "Invalid argument #2.";
		}
	} else {
		return "Invalid number of arguments.";
	}
	return 0;
}
#endif // CONFIG_I2C_XCMD

#endif
