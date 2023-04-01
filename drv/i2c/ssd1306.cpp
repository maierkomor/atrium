/*
 *  Copyright (C) 2021-2023, Thomas Maier-Komor
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

#ifdef CONFIG_SSD1306

#include "ssd1306.h"
#include "log.h"
#include "profiling.h"

#include "fonts_ssd1306.h"
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

#define CMD_NOP		0xe3

#define TAG MODULE_SSD130X



SSD1306 *SSD1306::Instance = 0;


SSD1306::SSD1306(uint8_t bus, uint8_t addr)
: I2CDevice(bus,addr,drvName())
{
	Instance = this;
}


SSD1306::~SSD1306()
{
	free(m_disp);
}


int SSD1306::init(uint8_t maxx, uint8_t maxy, uint8_t hwcfg)
{
	log_info(TAG,"init(%u,%u)",maxx,maxy);
	m_maxx = maxx;
	m_maxy = maxy;
	uint32_t dsize = maxx * maxy;
	m_disp = (uint8_t *) malloc(dsize); // two dimensional array of n pages each of n columns.
	if (m_disp == 0) {
		log_error(TAG,"out of memory");
		return 1;
	}
	uint8_t setup[] = {
		m_addr,
		0x00,				// command
		0xae,				// display off
		0xd5, 0x80,			// oszi freq (default), clock div=1 (optional)
		0xa8, (uint8_t)(m_maxy-1),	// MUX
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
		0xa4,				// output RAM
		0xa6,				// normal mode, a7=inverse
		0x2e,				// no scrolling
	};
	if (i2c_write(m_bus,setup,sizeof(setup),1,1))
		return 1;
	clear();
	sync();
	setOn(true);
	initOK();
	log_info(TAG,"ready");
	return 0;
}

int SSD1306::setOn(bool on)
{
	log_dbug(TAG,"setOn(%d)",on);
	uint8_t cmd_on[] = { m_addr, 0x00, 0x8d, 0x1f, 0xaf };
	uint8_t cmd_off[] = { m_addr, 0x00, 0xae, 0x8d, 0x10 };
	return i2c_write(m_bus,on ? cmd_on : cmd_off,sizeof(cmd_on),1,1);
}

int SSD1306::setInvert(bool inv)
{
	log_dbug(TAG,"invert(%d)",inv);
	uint8_t cmd[3] = { m_addr, 0x00, 0xa6 };
	if (inv)
		cmd[2] |= 1;
	return i2c_write(m_bus,cmd,sizeof(cmd),1,1);
}


int SSD1306::setContrast(uint8_t contrast)
{
	uint8_t cmd[] = { m_addr, 0x00, 0x81, contrast };
	return i2c_write(m_bus,cmd,sizeof(cmd),1,1);
}


int SSD1306::clear()
{
	uint8_t numpg = m_maxy >> 3;
	uint8_t pg = 0;
	do { 
		uint8_t *at = m_disp + m_maxx * pg;
		uint8_t *pge = at + m_maxx;
		do {
			if (*at)
				break;
			++at;
		} while (at != pge);
		if (at != pge) {
			bzero(at, pge-at);
			m_dirty |= 1 << pg;
		}
	} while (++pg != numpg);
	m_posx = 0;
	m_posy = 0;
	log_dbug(TAG,"clear: dirty %x",m_dirty);
	return 0;
}


uint8_t SSD1306::fontHeight() const
{
	switch (m_font) {
	case -1: return 8;
	case -2: return 16;
	default:
		return Fonts[m_font].yAdvance;
	}
}


int SSD1306::clrEol()
{
	clearRect(m_posx,m_posy,m_maxx-m_posx,fontHeight());
	return 0;
}


uint8_t SSD1306::charsPerLine() const
{
	if (m_font == font_nativedbl)
		return m_maxx/CHAR_WIDTH<<1;
	return m_maxx/CHAR_WIDTH;
}


uint8_t SSD1306::numLines() const
{
	return m_maxy/fontHeight();
}


int SSD1306::setFont(const char *fn)
{
	if (0 == strcasecmp(fn,"native")) {
		m_font = (fontid_t)-1;
		return 0;
	}
	if (0 == strcasecmp(fn,"nativedbl")) {
		m_font = (fontid_t)-2;
		return 0;
	}
	for (int i = 0; i < font_numfonts; ++i) {
		if (0 == strcasecmp(Fonts[i].name,fn)) {
			m_font = (fontid_t)i;
			return 0;
		}
	}
	return -1;
}


int SSD1306::sync()
{
	if (m_dirty == 0)
		return 0;
	PROFILE_FUNCTION();
	uint8_t cmd[] = { m_addr, 0x00, 0xb0, 0x21, 0x00, (uint8_t)(m_maxx-1) };
	uint8_t pfx[] = { m_addr, 0x40 };
	uint8_t numpg = m_maxy / 8 + ((m_maxy & 7) != 0);
	unsigned pgs = m_maxx;
	if (pgs == 128) {
		if (m_dirty == 0xff) {
			i2c_write(m_bus,cmd,sizeof(cmd),1,1);
			i2c_write(m_bus,pfx,sizeof(pfx),0,1);
			i2c_write(m_bus,m_disp,128*8,1,0);
			log_dbug(TAG,"sync 0-7");
			m_dirty = 0;
		} else if (m_dirty == 0xf) {
			i2c_write(m_bus,cmd,sizeof(cmd),1,1);
			i2c_write(m_bus,pfx,sizeof(pfx),0,1);
			i2c_write(m_bus,m_disp,128*4,1,0);
			log_dbug(TAG,"sync 0-3");
			m_dirty = 0;
		}
	}
	if (m_dirty) {
		for (uint8_t p = 0; p < numpg; p++) {
			if (m_dirty & (1<<p)) {
				i2c_write(m_bus,cmd,sizeof(cmd),1,1);
				i2c_write(m_bus,pfx,sizeof(pfx),0,1);
				i2c_write(m_bus,m_disp+p*pgs,pgs,1,0);
				log_dbug(TAG,"sync %u",p);
			}
			++cmd[2];
		}
		m_dirty = 0;
	}
	return 0;
}


int SSD1306::readByte(uint8_t x, uint8_t y, uint8_t *b)
{
	if ((x >= m_maxx) || (y >= m_maxy)) {
		log_dbug(TAG,"off display %u,%u",x,y);
		return 1;
	}
	uint8_t shift = y & 7;
	uint8_t pg = y >> 3;
	uint16_t idx = pg * m_maxx + x;
	uint8_t b0 = m_disp[idx];
	if (shift) {
		b0 >>= shift;
		idx += m_maxx;
		if (idx < (m_maxx*m_maxy)) {
			uint8_t b1 = m_disp[idx];
			b1 <<= (8-shift);
			b0 |= b1;
		}
	}
	*b = b0;
	return 0;
}


int SSD1306::drawMasked(uint8_t x, uint8_t y, uint8_t b, uint8_t m)
{
	uint8_t o;
	if (readByte(x,y,&o))
		return 1;
	o &= ~m;
	o |= (b & m);
	return drawByte(x,y,o);
}


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

int SSD1306::drawBits(uint8_t x, uint8_t y, uint8_t b, uint8_t n)
{
	static const uint8_t masks[] = {0x1,0x3,0x7,0xf,0x1f,0x3f,0x7f};
	b &= masks[n-1];
//	log_dbug(TAG,"drawBits(%u,%u,%x,%u)",x,y,b,n);
	uint8_t pg = y >> 3;
	unsigned off = pg * m_maxx + x;
	uint8_t shl = y & 7;
	uint16_t b0 = (uint16_t)b << shl;
	uint8_t b1 = (uint8_t)(b0 >> 8);
	if (b1)
		m_disp[off+m_maxx] |= b1;
	m_disp[off] |= (uint8_t)(b0&0xff);
//	log_dbug(TAG,"drawBits %x at %u",b0,off);
	return 0;
}


int SSD1306::drawByte(uint8_t x, uint8_t y, uint8_t b)
{
	uint8_t pg = y >> 3;
	uint16_t idx = pg * m_maxx + x;
	if ((x >= m_maxx) || (y >= m_maxy)) {
		log_dbug(TAG,"off display %u,%u=%u pg=%u",(unsigned)x,(unsigned)y,(unsigned)idx,(unsigned)pg);
		return 1;
	}
	uint8_t shift = y & 7;
	if (shift != 0) {
		uint16_t idx2 = idx + m_maxx;
		if (idx2 >= (m_maxx*m_maxy))
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


int SSD1306::drawChar(char c)
{
	PROFILE_FUNCTION();
	switch ((unsigned char) c) {
	case '\r':
		m_posx = 0;
		return 0;
	case '\n':
		m_posx = 0;
		m_posy += fontHeight();
		return 0;
	case 176:	// '°'
		c = 133;
		break;
	case 196:	// 'Ä'
		c = 130;
		break;
	case 220:	// 'Ü'
		c = 128;
		break;
	case 214:	// 'Ö'
		c = 132;
		break;
	case 223:	// 'ß'
		c = 134;
		break;
	case 228:	// 'ä'
		c = 129;
		break;
	case 246:	// 'ö'
		c = 131;
		break;
	case 252:	// 'ü'
		c = 127;
		break;
	default:
		break;
	}
	uint8_t x = m_posx;
	if (m_font == -1) {
		if (c < 32)
			return 1;
		uint16_t idx = (c - 32)*6;
		if (idx >= SizeofFont6x8)
			return 1;
		for (int c = 0; c < 6; ++c) 
			drawByte(x++, m_posy, Font6x8[idx+c]);
		m_posx = x;
		return 0;
	} else if (m_font == -2) {
		if (c < 32)
			return 1;
		uint16_t idx = (c - 32)*6;
		if (idx >= SizeofFont6x8)
			return 1;
		for (int c = 0; c < 6; ++c) {
			uint16_t w = scaleDouble(Font6x8[idx+c]);
			drawByte(x, m_posy, w & 0xff);
			drawByte(x, m_posy+8, w >> 8);
			++x;
			drawByte(x, m_posy, w & 0xff);
			drawByte(x, m_posy+8, w >> 8);
			++x;
		}
		m_posx = x;
		return 0;
	}
	const Font *font = Fonts+(int)m_font;
	if ((font < Fonts) || (font >= Fonts+(int)font_numfonts)) {
		log_dbug(TAG,"invalid font");
		return 1;
	}
	if ((c < font->first) || (c > font->last))
		return 1;
	uint8_t ch = c - font->first;
	const uint8_t *off = font->bitmap + font->glyph[ch].bitmapOffset;
	uint8_t w = font->glyph[ch].width;
	uint8_t h = font->glyph[ch].height;
	int8_t dx = font->glyph[ch].xOffset;
	int8_t dy = font->glyph[ch].yOffset;
	uint8_t a = font->glyph[ch].xAdvance;
	log_dbug(TAG,"drawChar('%c') at %u/%u %ux%u",c,m_posx,m_posy,w,h);
//	log_info(TAG,"%d/%d %+d/%+d, adv %u len %u",(int)w,(int)h,(int)dx,(int)dy,a,l);
//	clearRect(m_posx,m_posy,dx+w,a);
//	drawBitmap(m_posx+dx,m_posy+dy+font->yAdvance,w,h,off);
	drawBitmap_ssd1306(m_posx+dx,m_posy+dy+font->yAdvance-1,w,h,off);
	m_posx += a;

	return 0;
}


int SSD1306::drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data)
{
	unsigned len = w*h;
	log_dbug(TAG,"drawBitmap(%u,%u,%u,%u) %u/%u",x,y,w,h,len,len/8);
	unsigned idx = 0;
	uint8_t b = 0;
	while (idx != len) {
		if ((idx & 7) == 0)
			b = data[idx>>3];
		if (b&0x80)
			setPixel(x+idx%w,y+idx/w);
		else
			clrPixel(x+idx%w,y+idx/w);
		b<<=1;
		++idx;
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


int SSD1306::drawBitmap_ssd1306(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data)
{
	static const uint8_t masks[] = {0x1,0x3,0x7,0xf,0x1f,0x3f,0x7f};
	unsigned len = w*h;
	uint16_t bitoff = 0;
	log_dbug(TAG,"drawBitmap_fast(%u,%u,%u,%u) %u/%u",x,y,w,h,len,len/8);
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
					unsigned off = pg * m_maxx + x0;
					uint8_t shl = yoff & 7;
					uint16_t b0 = (uint16_t)byte << shl;
					m_disp[off] |= (uint8_t)(b0&0xff);
					uint8_t b1 = (uint8_t)(b0 >> 8);
					if (b1)
						m_disp[off+m_maxx] |= b1;
				}
				bitoff += numb;
				break;
			}
		}
	}
	return 0;
}


int SSD1306::clrPixel(uint16_t x, uint16_t y)
{
//	log_dbug(TAG,"setPixel(%u,%u)",(unsigned)x,(unsigned)y);
	if ((x < m_maxx) && (y < m_maxy)) {
		uint8_t pg = y >> 3;
		uint8_t *p = m_disp + pg * m_maxx + x;
		uint8_t bit = 1 << (y & 7);
		uint8_t b = *p;
		if ((b & bit) != 0) {
			*p = b & ~bit;
			m_dirty |= (1 << pg);
		}
		return 0;
	}
	return -1;
}


int SSD1306::setPixel(uint16_t x, uint16_t y)
{
//	log_dbug(TAG,"setPixel(%u,%u)",(unsigned)x,(unsigned)y);
	if ((x < m_maxx) && (y < m_maxy)) {
		uint8_t pg = y >> 3;
		uint8_t *p = m_disp + pg * m_maxx + x;
		uint8_t bit = 1 << (y & 7);
		uint8_t b = *p;
		if ((b & bit) == 0) {
			*p = b | bit;
			m_dirty |= (1 << pg);
		}
		return 0;
	}
	return -1;
}


void SSD1306::drawHLine(uint8_t x, uint8_t y, uint8_t n)
{
	if ((x + n > m_maxx) || (y >= m_maxy))
		return;
	uint8_t pg = y >> 3;
	uint16_t off = x + pg * m_maxx;
	uint8_t m = 1 << (y & 7);
	uint8_t *p = m_disp+off;
	do {
		m |= *p;
		if (*p != m) {
			*p = m;
			m_dirty |= (1 << pg);
		}
		++p;
	} while (--n);
}


void SSD1306::drawVLine(uint8_t x, uint8_t y, uint8_t n)
{
	while (n) {
		uint8_t pg = y >> 3;
		uint16_t off = x + pg * m_maxx;
		uint8_t shift = y & 7;
		uint8_t m = 0;
		if ((shift == 0) && (n >= 8)) {
			m = 0xff;
			n -= 8;
			y += 8;
		} else {
			m = 0;
			do {
				m |= (1 << shift);
				--n;
				++y;
				++shift;
			} while ((shift < 8) && (n != 0));
		}
		uint8_t *p = m_disp + off;
		uint8_t v = *p | m;
		if (v != *p) {
			*p = v;
			m_dirty |= (1<<pg);
		}
	}
}


int SSD1306::clearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	log_dbug(TAG,"clearRect(%u,%u,%u,%u)",x,y,w,h);
	if ((x > m_maxx) || (y >= m_maxy))
		return 1;
	for (int i = x; i < x+w; ++i) {
		uint16_t y0 = y;
		uint16_t h0 = h;
		do {
			if (((y & 7) == 0) && (h0 >= 8)) {
				drawByte(i,y0,0);
				y0 += 8;
				h0 -= 8;
			} else {
				clrPixel(i,y0);
				++y0;
				--h0;
			}
		} while (h0 > 0);
	}
	return 0;
}


int SSD1306::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	log_dbug(TAG,"drawRect(%u,%u,%u,%u)",x,y,w,h);
	drawHLine(x,y,w);
	drawHLine(x,y+h-1,w);
	drawVLine(x,y,h);
	drawVLine(x+w-1,y,h);
	return 0;
}


int SSD1306::writeHex(uint8_t h, bool comma)
{
	log_dbug(TAG,"writeHex %x",h);
	char c = h;
	if (h < 10)
		c += '0';
	else
		c += 'A' - 10;
	if (drawChar(c))
		return 1;
	if (comma) {
		if (drawChar('.'))
			return 1;
	}
	return 0;
}


int SSD1306::setPos(uint16_t x, uint16_t y)
{
	log_dbug(TAG,"setPos(%u/%u)",x,y);
	x *= CHAR_WIDTH;
	y *= fontHeight();
	if ((x >= m_maxx-(CHAR_WIDTH)) || (y > m_maxy-fontHeight())) {
		log_dbug(TAG,"invalid pos %u/%u",x,y);
		return 1;
	}
	log_dbug(TAG,"setPos %u/%u",x,y);
	m_posx = x;
	m_posy = y;
	return 0;
}


int SSD1306::write(const char *text, int len)
{
	log_dbug(TAG,"write %s",text);
	size_t n = 0;
	while (len) {
		char c = *text++;
		if (c == 0)
			return n;
		drawChar(c);
		++n;
		--len;
	}
	return n;
}


SSD1306 *SSD1306::create(uint8_t bus, uint8_t addr)
{
	addr <<= 1;
	uint8_t cmd[] = { (uint8_t)addr, CMD_NOP };
	if (0 == i2c_write(bus,cmd,sizeof(cmd),1,1)) {
		return new SSD1306(bus,addr);
	}
	return 0;
}


unsigned ssd1306_scan(uint8_t bus)
{
	uint8_t cmd[] = { (0x3c << 1), CMD_NOP };
	if (0 == i2c_write(bus,cmd,sizeof(cmd),1,1)) {
		new SSD1306(bus,cmd[0]);
		return 1;
	}
	cmd[0] += 2;
	if (0 == i2c_write(bus,cmd,sizeof(cmd),1,1)) {
		new SSD1306(bus,cmd[0]);
		return 1;
	}
	return 0;
}

#endif
