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

#if defined CONFIG_PCF8574

#include "hd44780u.h"
#include "log.h"
#include "pcf8574.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <string.h>

#ifdef CONFIG_IDF_TARGET_ESP32
#define USE_BUSYFLAG	// TODO: use should be bound to I2C frequency
#endif

// RS->P0
// RW->P1
// Enable->P2
// A->P3	(grounded => should be forced to 1?)
// D7->P7
// D6->P6
// D5->P5
// D4->P4

#define RS ((uint8_t)(1<<0))	// data/not-command
#define RD (1<<1)	// read
#define EN ((uint8_t)(1<<2))
#define ON ((uint8_t)(1<<3))
#define HIGH4(x) ((uint8_t)(x&0xf0))
#define LOW4(x) ((uint8_t)((x<<4)&0xf0))
#define FUNC4BIT 0x20
#define FUNC8BIT 0x33

#define CMD_CLR_DISP	0x01
#define CMD_HOME	0x02
#define CMD_SET_MODE	0x04	// bit1: auto-increment/_decrement address, bit0: shift/_no-shift cursor
	#define CA_AUTOINC	(1<<1)
	#define CA_SHIFTDIS	(1<<0)
#define CMD_DISP_PWR	0x08	// bit2: on/off, bit1: cursor on/off, bit0: blink cursor
	#define CA_DISP_ON	(1<<2)
	#define CA_CURSOR_ON	(1<<1)
	#define CA_BLINK	(1<<0)
#define CMD_MOVE	0x10	// bit3: S/nC (screen/cursor), bit2: r/nl (right/left)
#define CMD_FUNC	0x20	// bit4: data length, bit3: lines, bit2: font
	#define CA_FUNC_8BIT	0x10
	#define CA_FUNC_NUML2	0x08
	#define CA_FUNC_FN1015	0x04
#define CMD_SET_CG	0x40
#define CMD_SET_DD	0x80


#define TAG MODULE_HD44780U

extern const char Version[];


int HD44780U::init()
{
	m_drv = PCF8574::getInstance();
	if (m_drv == 0)
		return 1;
	log_dbug(TAG,"init");
	// 8-bit init is required for 4-bit setup to work after reboot not
	// only power-up
	uint8_t setup[] = {
		HIGH4(FUNC8BIT)|ON|EN,
		HIGH4(FUNC8BIT)|ON,
		LOW4(FUNC8BIT)|ON|EN,
		LOW4(FUNC8BIT)|ON,
		HIGH4(FUNC4BIT)|ON|EN,
		HIGH4(FUNC4BIT)|ON,
	};
	m_drv->write(setup,sizeof(setup));
	writeCmd(CMD_FUNC|CA_FUNC_NUML2);
	writeCmd(CMD_SET_MODE|CA_AUTOINC);
	clear();
	initOK();
	return 0;
}


int HD44780U::clear()
{
	log_dbug(TAG,"clear");
	m_posx = 0;
	m_posy = 0;
	m_posinv = false;
	bzero(m_data,sizeof(m_data));
	int r = writeCmd(CMD_CLR_DISP);
	vTaskDelay(2);	// not good but needed...
	return r;
}


bool HD44780U::hasChar(char c) const
{
	return true;
}


static int8_t pos2off(uint8_t posx, uint8_t posy, uint8_t maxx, uint8_t maxy)
{
	static const int8_t off[] = {0,0x40,0x20,0x60};
	int8_t o = -1;
	if ((posx < maxx) && (posy < maxy))
		o = off[posy]+posx;
//	log_dbug(TAG,"pos2off(%u/%u) %d",(unsigned) posx, (unsigned) posy, (int)o);
	return o;
}


int HD44780U::setPos(uint8_t x, uint8_t y)
{
#if 0
	if ((m_posx == x) && (m_posy == y) && !m_posinv)
		return 0;
	if (x >= m_maxx)
		x = m_maxx-1;
	if (y >= m_maxy)
		y = m_maxy-1;
	int8_t off = pos2off(x,y,m_maxx,m_maxy);
	log_dbug(TAG,"setPos(%u,%u): %d",(unsigned)x,(unsigned)y,(int)off);
	if (off < 0)
		return -1;
	m_posx = x;
	m_posy = y;
	m_posinv = false;
	return writeCmd(CMD_SET_DD|off);
#else
	if ((m_posx != x) || (m_posy != y)) {
		log_dbug(TAG,"setPos(%u,%u)",(unsigned)x,(unsigned)y);
		m_posx = x;
		m_posy = y;
		m_posinv = true;
	}
	return 0;
#endif
}


int HD44780U::setOn(bool on)
{
	if (on)
		m_disp |= CA_DISP_ON;
	else
		m_disp &= ~CA_DISP_ON;
	return writeCmd(CMD_DISP_PWR|m_disp);
}


int HD44780U::setCursor(bool cursor)
{
	if (cursor)
		m_disp |= CA_CURSOR_ON;
	else
		m_disp &= ~CA_CURSOR_ON;
	return writeCmd(CMD_DISP_PWR|m_disp);
}


int HD44780U::setBlink(bool blink)
{
	if (blink)
		m_disp |= CA_BLINK;
	else
		m_disp &= ~CA_BLINK;
	return writeCmd(CMD_DISP_PWR|m_disp);
}


int HD44780U::setDim(uint8_t d)
{
	if (d > 1)
		return -1;
	return setOn(d);
}


void HD44780U::setDisplay(bool on, bool cursor, bool blink)
{
	if (on)
		m_disp |= CA_DISP_ON;
	else
		m_disp &= ~CA_DISP_ON;
	if (cursor)
		m_disp |= CA_CURSOR_ON;
	else
		m_disp &= ~CA_CURSOR_ON;
	if (blink)
		m_disp |= CA_BLINK;
	else
		m_disp &= ~CA_BLINK;
	writeCmd(CMD_DISP_PWR|m_disp);
}


int HD44780U::write(const char *t, int n)
{
	log_dbug(TAG,"write('%s',%d) at %u/%u",t,n,m_posx,m_posy);
	while (*t && (n != 0)) {
		char c = *t;
		if (c == '\n') {
			++m_posy;
			m_posx = 0;
			m_posinv = true;
		} else if (c == '\r') {
			m_posx = 0;
			m_posinv = true;
		} else {
			if (c == 0260)
				c = 0xdf;
			if (writeData(c))
				return 1;
		}
		--n;
		++t;
	}
	log_dbug(TAG,"pos %u/%u",m_posx,m_posy);
	return 0;
}


int HD44780U::writeHex(uint8_t h, bool c)
{
	log_dbug(TAG,"writeHex('%x',%d)",(unsigned)h,c);
	if (writeData(h < 10 ? h + '0' : h + 'A' - 10))
		return -1;
	if (c)
		return writeData('.');
	return 0;
}


int HD44780U::writeCmd(uint8_t d)
{
	log_dbug(TAG,"cmd 0x%02x",(unsigned)d);
	uint8_t cmd[] = {
		(uint8_t) (ON|HIGH4(d)|EN),
		(uint8_t) (ON|HIGH4(d)),
		(uint8_t) (ON|LOW4(d)|EN),
		(uint8_t) (ON|LOW4(d)),
	};
	uint8_t wait = 0;
#ifdef USE_BUSYFLAG
	while (0x80 & readBusy()) ++wait;
#else
	uint32_t now;
	do {
		now = esp_timer_get_time() & 0xffffffff;
		++wait;
	} while ((now - m_lcmd < 40) && (now - m_lcmd > 0));
#endif
	int r = m_drv->write(cmd,sizeof(cmd));
#ifndef USE_BUSYFLAG
	m_lcmd = esp_timer_get_time() & 0xffffffff;
#endif
	log_dbug(TAG,"waitC %u",wait);
	return r;
}


int HD44780U::writeData(uint8_t d)
{
	int8_t at = pos2off(m_posx,m_posy,m_maxx,m_maxy);
	if (at < 0) {
		log_dbug(TAG,"invalid position");
		return -1;
	}
#if 0	// variant without caching
	uint8_t data[] = {
		(uint8_t) (RS|EN|ON|HIGH4(d)),
		(uint8_t) (RS|ON|HIGH4(d)),
		(uint8_t) (RS|EN|ON|LOW4(d)),
		(uint8_t) (RS|ON|LOW4(d)),
	};
	uint8_t wait = 0;
#ifdef USE_BUSYFLAG
	while (0x80 & readBusy()) ++wait;
#else
	uint32_t now;
	do {
		now = esp_timer_get_time() & 0xffffffff;
		++wait;
	} while ((now - m_lcmd < 40) && (now - m_lcmd > 0));
#endif
	//		log_dbug(TAG,"wait %u",wait);
	int r = m_drv->write(data,sizeof(data));
#ifdef USE_BUSYFLAG
	m_lcmd = esp_timer_get_time() & 0xffffffff;
#endif
	if (++m_posx == m_maxx) {
		m_posx = 0;
		if (++m_posy == m_maxy)
			m_posy = 0;
	}
	return r;
#else
	if (at >= sizeof(m_data)) {
		log_dbug(TAG,"off screen");
		return 1;
	}
	int r = 0;
	log_dbug(TAG,"m_data[%d]=0x%x <= 0x%x ",(int)at,(unsigned)m_data[at],(unsigned)d);
	if (m_data[at] != d) {
		if (m_posinv) {
			log_dbug(TAG,"re-pos %u,%u=%d",m_posx,m_posy,(int)at);
			writeCmd(CMD_SET_DD|at);
			m_posinv = false;
		}
		m_data[at] = d;
		uint8_t data[] = {
			(uint8_t) (RS|EN|ON|HIGH4(d)),
			(uint8_t) (RS|ON|HIGH4(d)),
			(uint8_t) (RS|EN|ON|LOW4(d)),
			(uint8_t) (RS|ON|LOW4(d)),
		};
		uint8_t wait = 0;
#ifdef USE_BUSYFLAG
		while (0x80 & readBusy()) ++wait;
#else
		uint32_t now;
		do {
			now = esp_timer_get_time() & 0xffffffff;
			++wait;
		} while ((now - m_lcmd < 40) && (now - m_lcmd > 0));
#endif
		log_dbug(TAG,"waitD %u",wait);
		r = m_drv->write(data,sizeof(data));
#ifndef USE_BUSYFLAG
		m_lcmd = esp_timer_get_time() & 0xffffffff;
#endif
		log_dbug(TAG,"disp[%d]=0x%x",(int)at,(unsigned)d);
	} else {
		m_posinv = true;
		log_dbug(TAG,"data[%d] OK",(int)at);
	}
	++m_posx;
	/*
	if (++m_posx == m_maxx) {
		m_posx = 0;
		if (++m_posy == m_maxy)
			m_posy = 0;
	}
	*/
	return r;
#endif
}


uint8_t HD44780U::readBusy()
{
	/*
	m_drv->write(ON|EN|HIGH4(0xff));
	m_drv->write(ON|HIGH4(0xff));
	m_drv->write(ON|EN|LOW4(0xff));
	m_drv->write(ON|LOW4(0xff));
	m_drv->write(ON|RD|EN|HIGH4(0xff));
	uint8_t rh = m_drv->read();
	m_drv->write(ON|RD|HIGH4(0xff));
	m_drv->write(ON|RD|EN|HIGH4(0xff));
	uint8_t rl = m_drv->read();
	m_drv->write(ON|RD|HIGH4(0xff));
	log_dbug(TAG,"busy %x,%x",rh,rl);
	return (rh&0xf0)|(rl>>4);
	*/
	m_drv->write(ON|RD|HIGH4(0xff));
	m_drv->write(ON|RD|EN|HIGH4(0xff));
	uint8_t rh = m_drv->read();
	m_drv->write(ON|RD|HIGH4(0xff));
	m_drv->write(ON|RD|EN|HIGH4(0xff));
	uint8_t rl = m_drv->read();
	m_drv->write(ON|HIGH4(0xff));
	log_dbug(TAG,"busy %x,%x",rh,rl);
	return (rh&0xf0)|(rl>>4);
}


int HD44780U::clrEol()
{
	uint8_t x = m_posx;
	log_dbug(TAG,"clrEol() y=%u",(unsigned)m_posy);
	do
		writeData(' ');
	while (m_posx < m_maxx);
	m_posx = x;
	log_dbug(TAG,"clrEol(): done");
	return 0;
}

#endif
