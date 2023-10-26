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

#ifdef CONFIG_SH1106

#include "sh1106.h"
#include "log.h"
#include "profiling.h"

//#include "fonts.h"
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define CHAR_WIDTH	6
#define CHAR_HEIGHT	8

#define CTRL_CMD1	0x00	// command and data
#define CTRL_CMDN	0x80	// command with more commands
#define CTRL_CMDC	0xc0	// continuation command
#define CTRL_DATA	0x00	// data only

#define SH1106_ADDR0	0x3c
#define SH1106_ADDR1	0x3d

#define CMD_NOP		0xe3

#define TAG MODULE_SSD130X



SH1106 *SH1106::Instance = 0;


SH1106::SH1106(uint8_t bus, uint8_t addr)
: I2CDevice(bus,addr,drvName())
{
	Instance = this;
	log_info(TAG,"ssd1306 at %u,0x%x",bus,addr);
}


int SH1106::init(uint8_t maxx, uint8_t maxy, uint8_t hwcfg)
{
	log_info(TAG,"init(%u,%u)",maxx,maxy);
	m_width = maxx;
	m_height = maxy;
	uint32_t dsize = maxx * maxy;
	m_disp = (uint8_t *) malloc(dsize); // two dimensional array of n pages each of n columns.
	if (m_disp == 0) {
		log_error(TAG,"out of memory");
		return 1;
	}
	/*
	uint8_t setup[] = {
		m_addr,
		0xae,				// display off
		0xa4,				// output RAM
		0xaf,
		0x00,				// set lower column start address
		0xd5, 0x80,			// oszi freq (default), clock div=1 (optional)
		0xa8, (uint8_t)(m_height-1),	// MUX
		0xd3, 0x00,			// display offset (optional)
		0x40,				// display start line	(optional)
		0x8d, 0x14,			// enable charge pump
		0x20, 0x00,			// address mode: horizontal
		0xa0,				// map address 0 to seg0
		(uint8_t) (0xc0 | (hwcfg&hwc_iscan)),	// scan 0..n
		0xda,				// COM hardware config
		(uint8_t) (hwcfg&(hwc_rlmap|hwc_altm)),	
		0x81, 0x80,			// medium contrast
		0xd9, 0x22,			// default pre-charge (optional)
		0xa6,				// normal mode, a7=inverse
		0x2e,				// no scrolling
	};
	if (i2c_write(m_bus,setup,sizeof(setup),1,1))
		return 1;
		*/
	xmitCmd(0xae);
	xmitCmd(0xa4);
	vTaskDelay(100);
	clear();
	flush();
	setOn(true);
	initOK();
	log_info(TAG,"ready");
	return 0;
}


void SH1106::xmitCmds(uint8_t *cmd, unsigned n)
{
	do {
		xmitCmd(*cmd);
		++cmd;
		--n;
	} while (n);
}


int SH1106::xmitCmd(uint8_t cmd)
{
	log_dbug(TAG,"xmitCmd(0x%x)",cmd);
	return i2c_write2(m_bus,m_addr,0x00,cmd);
}


int SH1106::setOn(bool on)
{
	log_dbug(TAG,"setOn(%d)",on);
	return xmitCmd(0xae|on);
	/*
	uint8_t cmd_on[] = { m_addr, 0x00, 0xaf };
	uint8_t cmd_off[] = { m_addr, 0x00, 0xae };
	return i2c_write(m_bus,on ? cmd_on : cmd_off,sizeof(cmd_on),1,1);
	*/
}

int SH1106::setInvert(bool inv)
{
	log_dbug(TAG,"invert(%d)",inv);
	return xmitCmd(0xa6|inv);
	/*
	uint8_t cmd[3] = { m_addr, 0x00, 0xa6 };
	if (inv)
		cmd[2] |= 1;
	return i2c_write(m_bus,cmd,sizeof(cmd),1,1);
	*/
}


int SH1106::setBrightness(uint8_t contrast)
{
	uint8_t cmd[] = { m_addr, 0x00, 0x81, contrast };
	return i2c_write(m_bus,cmd,sizeof(cmd),1,1);
}


void SH1106::flush()
{
	if (m_dirty == 0)
		return;
	PROFILE_FUNCTION();
	uint8_t cmd[] = { m_addr, 0x00, 0x00, 0x10, 0xb0 };
	uint8_t pfx[] = { m_addr, 0x40 };
	uint8_t numpg = m_height / 8 + ((m_height & 7) != 0);
	unsigned pgs = m_width;
	if (m_dirty) {
		for (uint8_t p = 0; p < numpg; p++) {
			if (m_dirty & (1<<p)) {
				cmd[4] = 0xb0 + p;
				i2c_write(m_bus,cmd,sizeof(cmd),1,1);
				i2c_write(m_bus,pfx,sizeof(pfx),0,1);
				i2c_write(m_bus,m_disp+p*pgs,pgs,1,0);
				log_dbug(TAG,"sync %u",p);
			}
		}
		m_dirty = 0;
	}
}


int SH1106::readByte(uint8_t x, uint8_t y, uint8_t *b)
{
	if ((x >= m_width) || (y >= m_height)) {
		log_dbug(TAG,"off display %u,%u",x,y);
		return 1;
	}
	uint8_t shift = y & 7;
	uint8_t pg = y >> 3;
	uint16_t idx = pg * m_width + x;
	uint8_t b0 = m_disp[idx];
	if (shift) {
		b0 >>= shift;
		idx += m_width;
		if (idx < (m_width*m_height)) {
			uint8_t b1 = m_disp[idx];
			b1 <<= (8-shift);
			b0 |= b1;
		}
	}
	*b = b0;
	return 0;
}


int SH1106::drawMasked(uint8_t x, uint8_t y, uint8_t b, uint8_t m)
{
	uint8_t o;
	if (readByte(x,y,&o))
		return 1;
	o &= ~m;
	o |= (b & m);
	return drawByte(x,y,o);
}


/*
static uint16_t scaleDouble(uint8_t byte)
{
      uint16_t r = 0;
      uint16_t m = 1;
      for (uint8_t b = 0; b < 8; ++b) {
              if (byte & (1<<b)) {
                      r |= m;
                      m <<= 1;
                      r |= m;
                      m <<= 1;
              } else {
                      m <<= 2;
              }
      }
      return r;
}
*/


/*
 *  xxxxXXXX
 *  00000000vvvVVVVV	vertical offset
 *  shl 5
 *  XXXXxxxxx	byte0
 *  shr 3
 *  xxxxxxxX	byte1
 *
 *  vVVVVVVV	7 used, 1 bit b0
 *  xxxXXXXX	5 bits to write
 *  Aaaaaaaa	byte0: shl7
 *  bbbbbbbB	byte1: shr1
 *
 *  00000000 vvvvVVVV	4 used, 4bit b0
 *           xxxXXXXX	5 bits to write
 *  000000AA AAAaaaaa
 *  byte1    byte0
 */

int SH1106::drawBits(uint8_t x, uint8_t y, uint8_t b, uint8_t n)
{
	static const uint8_t masks[] = {0x1,0x3,0x7,0xf,0x1f,0x3f,0x7f};
	b &= masks[n-1];
//	log_dbug(TAG,"drawBits(%u,%u,%x,%u)",x,y,b,n);
	uint8_t pg = y >> 3;
	unsigned off = pg * m_width + x;
	uint8_t shl = y & 7;
	uint16_t b0 = (uint16_t)b << shl;
	uint8_t b1 = (uint8_t)(b0 >> 8);
	if (b1)
		m_disp[off+m_width] |= b1;
	m_disp[off] |= (uint8_t)(b0&0xff);
//	log_dbug(TAG,"drawBits %x at %u",b0,off);
	return 0;
}


int SH1106::drawByte(uint8_t x, uint8_t y, uint8_t b)
{
	uint8_t pg = y >> 3;
	uint16_t idx = pg * m_width + x;
	if ((x >= m_width) || (y >= m_height)) {
		log_dbug(TAG,"off display %u,%u=%u pg=%u",(unsigned)x,(unsigned)y,(unsigned)idx,(unsigned)pg);
		return 1;
	}
	uint8_t shift = y & 7;
	if (shift != 0) {
		uint16_t idx2 = idx + m_width;
		if (idx2 >= (m_width*m_height))
			return 1;
		m_dirty |= 1<<(pg+1);
		uint16_t w = (uint16_t) b << shift;
		uint16_t m = 0xff << shift;
		m = ~m;
		uint8_t b0 = (m_disp[idx] & m) | (w & 0xFF);
		if (b0 != m_disp[idx]) {
			m_disp[idx] = b0;
			m_dirty |= 1<<pg;
		}
		b = (m_disp[idx2] & (m >> 8)) | (w >> 8);
		idx = idx2;
	}
	if (m_disp[idx] != b) {
		m_dirty |= 1<<pg;
		m_disp[idx] = b;
	}
	return 0;
}


static uint8_t getBits(const uint8_t *data, unsigned off, uint8_t numb)
{
	unsigned b = off >> 3;
	uint8_t byte = data[b];
	unsigned bitst = off & 7;
	uint8_t got = 8-bitst;
	byte >>= bitst;
	if (got < numb)
		byte |= data[b+1] << got;
//	log_dbug(TAG,"getBits(%u,%u): %x",off,numb,byte);
	return byte;
}


void SH1106::drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg, int32_t bg)
{
	static const uint8_t masks[] = {0x1,0x3,0x7,0xf,0x1f,0x3f,0x7f};
	unsigned len = w*h;
	uint16_t bitoff = 0;
	log_dbug(TAG,"SH1106::drawBitmap(%u,%u,%u,%u) %u/%u",x,y,w,h,len,len/8);
	for (uint8_t x0 = x; x0 < x+w; ++x0) {
		uint8_t yoff = y;
		uint8_t numb = h;
		while (numb) {
//			log_dbug(TAG,"numb=%u",numb);
			uint8_t byte = getBits(data,bitoff,numb);
			if (numb >= 8) {
//				log_dbug(TAG,"byte %x at %u/%u",byte,x0,y+yoff);
				if (byte)
					drawByte(x0,yoff,byte);
				numb -= 8;
				yoff += 8;
				bitoff += 8;
			} else {
				byte &= masks[numb-1];
				if (byte) {
//					drawBits(x0,yoff,byte,numb);
					uint8_t pg = yoff >> 3;
					unsigned off = pg * m_width + x0;
					uint8_t shl = yoff & 7;
					uint16_t b0 = (uint16_t)byte << shl;
					m_disp[off] |= (uint8_t)(b0&0xff);
					uint8_t b1 = (uint8_t)(b0 >> 8);
					if (b1)
						m_disp[off+m_width] |= b1;
				}
				bitoff += numb;
				break;
			}
		}
	}
}



SH1106 *SH1106::create(uint8_t bus, uint8_t addr)
{
	addr <<= 1;
	uint8_t cmd[] = { (uint8_t)addr, 0x00, CMD_NOP };
	if (0 == i2c_write(bus,cmd,sizeof(cmd),1,1)) {
		return new SH1106(bus,addr);
	}
	return 0;
}


unsigned sh1106_scan(uint8_t bus)
{
	log_dbug(TAG,"searching for SH1106");
	uint8_t addrs[] = { 0x3c, 0x3d };
	for (uint8_t addr : addrs) {
		if (SH1106::create(bus,addr))
			return 1;
	}
	return 0;
}

#endif
