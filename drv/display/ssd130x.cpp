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

#include "ssd130x.h"
#include "log.h"
#include "profiling.h"

//#include "fonts_ssd1306.h"
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


/*
static Font NativeFont = {
	.name = "native",
	.bitmap = Font6x8,
	.glyph = 0,
	.first = 32,
	.last = 0,
	.yAdvance = 8,
};
*/


SSD130X::~SSD130X()
{
	free(m_disp);
}


void SSD130X::clear()
{
	uint8_t numpg = m_height >> 3;
	uint8_t pg = 0;
	do { 
		uint8_t *at = m_disp + m_width * pg;
		uint8_t *pge = at + m_width;
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
}


void SSD130X::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col)
{
	log_dbug(TAG,"fillRect(%u,%u,%u,%u)",x,y,w,h);
	if ((x > m_width) || (y >= m_height))
		return;
	if ((x+w) > m_width)
		w = m_width - x;
	if ((y+h) > m_height)
		h = m_height - y;
	for (int i = x; i < x+w; ++i) {
		uint16_t y0 = y;
		uint16_t h0 = h;
		do {
			if (((y & 7) == 0) && (h0 >= 8)) {
				drawByte(i,y0,0xff);
				y0 += 8;
				h0 -= 8;
			} else {
				setPixel(i,y0);
				++y0;
				--h0;
			}
		} while (h0 > 0);
	}
}


uint16_t SSD130X::fontHeight() const
{
	/*
	switch (m_font) {
	case -1: return 8;
	case -2: return 16;
	default:
		return Fonts[m_font].yAdvance;
	}
	*/
	return m_font->yAdvance;
}


uint16_t SSD130X::charsPerLine() const
{
	/*
	if (m_font == font_nativedbl)
		return m_width/CHAR_WIDTH<<1;
	*/
	return m_width/CHAR_WIDTH;
}


uint16_t SSD130X::numLines() const
{
	return m_height/fontHeight();
}


int SSD130X::setFont(const char *fn)
{
	/*
	if (0 == strcasecmp(fn,"native")) {
		m_font = (fontid_t)-1;
		return 0;
	}
	if (0 == strcasecmp(fn,"nativedbl")) {
		m_font = (fontid_t)-2;
		return 0;
	}
	*/
	for (int i = 0; i < NumFontsBCM; ++i) {
		if (0 == strcasecmp(FontsBCM[i].name,fn)) {
			m_font = FontsBCM+i;
			return 0;
		}
	}
	return -1;
}


int SSD130X::readByte(uint8_t x, uint8_t y, uint8_t *b)
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


int SSD130X::drawMasked(uint8_t x, uint8_t y, uint8_t b, uint8_t m)
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

int SSD130X::drawBits(uint8_t x, uint8_t y, uint8_t b, uint8_t n)
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


int SSD130X::drawByte(uint8_t x, uint8_t y, uint8_t b)
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


void SSD130X::drawChar(char c)
{
	if (c == '\r') {
		m_posx = 0;
	} else if (c == '\n') {
		m_posx = 0;
		m_posy += fontHeight();
	} else {
		m_posx += drawChar(m_posx, m_posy, c, 1, 0);
	}
}


unsigned SSD130X::drawChar(uint16_t x, uint16_t y, char c, int32_t fg, int32_t bg)
{
	PROFILE_FUNCTION();
	switch ((unsigned char) c) {
	case '\r':
//		m_posx = 0;
		return 0;
	case '\n':
//		m_posx = 0;
//		m_posy += fontHeight();
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
	/*
	if (m_font == -1) {
		if (c < 32)
			return 1;
		uint16_t idx = (c - 32)*6;
		if (idx >= SizeofFont6x8)
			return 1;
		for (int c = 0; c < 6; ++c) 
			drawByte(x++, m_posy, Font6x8[idx+c]);
		return 6;
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
		return 12;
	}
	const Font *font = Fonts+(int)m_font;
	if ((font < Fonts) || (font >= Fonts+(int)font_numfonts)) {
		log_dbug(TAG,"invalid font");
		return 1;
	}
	*/
	if ((c < m_font->first) || (c > m_font->last)) {
		log_dbug(TAG,"undefined char");
		return 1;
	}
	uint8_t ch = c - m_font->first;
	const uint8_t *off = m_font->bitmap + m_font->glyph[ch].bitmapOffset;
	uint8_t w = m_font->glyph[ch].width;
	uint8_t h = m_font->glyph[ch].height;
	int8_t dx = m_font->glyph[ch].xOffset;
	int8_t dy = m_font->glyph[ch].yOffset;
	uint16_t a = m_font->glyph[ch].xAdvance;
	log_dbug(TAG,"drawChar(%u,%u,'%c') with %ux%u",x,y,c,w,h);
//	log_info(TAG,"%d/%d %+d/%+d, adv %u len %u",(int)w,(int)h,(int)dx,(int)dy,a,l);
	clearRect(x,y,a,m_font->yAdvance);
	drawBitmapNative(x+dx,y+dy+m_font->yAdvance-1,w,h,off);
	return a;
}


void SSD130X::drawHLine(uint16_t x, uint16_t y, uint16_t n, int32_t col)
{
	PROFILE_FUNCTION();
	if ((x + n > m_width) || (y >= m_height) || (col != 1))
		return;
	if ((x + n) > m_width)
		n = m_width - x;
	uint8_t pg = y >> 3;
	uint16_t off = x + pg * m_width;
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


void SSD130X::drawVLine(uint16_t x, uint16_t y, uint16_t n, int32_t col)
{
	PROFILE_FUNCTION();
	if ((x >= m_width) || (y >= m_height) || (col != 1))
		return;
	if ((y + n) > m_height)
		n = m_height - y;
	while (n) {
		uint8_t pg = y >> 3;
		uint16_t off = x + pg * m_width;
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


static inline uint8_t getBits(const uint8_t *data, unsigned off, uint8_t numb)
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


void SSD130X::drawBitmapNative(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data)
{
	PROFILE_FUNCTION();
	static const uint8_t masks[] = {0x1,0x3,0x7,0xf,0x1f,0x3f,0x7f};
	unsigned len = w*h;
	uint16_t bitoff = 0;
	log_dbug(TAG,"drawBitmapNative(%u,%u,%u,%u) %u/%u",x,y,w,h,len,len/8);
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


inline void SSD130X::pClrPixel(uint16_t x, uint16_t y)
{
//	log_dbug(TAG,"clrPixel(%u,%u)",(unsigned)x,(unsigned)y);
	if ((x < m_width) && (y < m_height)) {
		uint8_t pg = y >> 3;
		uint8_t *p = m_disp + pg * m_width + x;
		uint8_t bit = 1 << (y & 7);
		uint8_t b = *p;
		if ((b & bit) != 0) {
			*p = b & ~bit;
			m_dirty |= (1 << pg);
		}
	}
}


void SSD130X::pSetPixel(uint16_t x, uint16_t y)
{
//	log_dbug(TAG,"setPixel(%u,%u)",(unsigned)x,(unsigned)y);
	if ((x < m_width) && (y < m_height)) {
		uint8_t pg = y >> 3;
		uint8_t *p = m_disp + pg * m_width + x;
		uint8_t bit = 1 << (y & 7);
		uint8_t b = *p;
		if ((b & bit) == 0) {
			*p = b | bit;
			m_dirty |= (1 << pg);
		}
	}
}


int SSD130X::setFont(unsigned f)
{
	if (f < NumFontsBCM) {
		m_font = FontsBCM + f;
		return 0;
	}
	return -1;
}


void SSD130X::setPixel(uint16_t x, uint16_t y, int32_t col)
{
//	log_dbug(TAG,"setPixel(%u,%u)",(unsigned)x,(unsigned)y);
	if ((x < m_width) && (y < m_height)) {
		uint8_t pg = y >> 3;
		uint8_t *p = m_disp + pg * m_width + x;
		uint8_t bit = 1 << (y & 7);
		uint8_t b = *p;
		if ((b & bit) == 0) {
			if (col == 1) {
				*p = b | bit;
				m_dirty |= (1 << pg);
			}
		} else {
			if (col == 0) {
				m_dirty &= ~(1 << pg);
			}
		}
	}
}


int SSD130X::clearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	PROFILE_FUNCTION();
	log_dbug(TAG,"clearRect(%u,%u,%u,%u)",x,y,w,h);
	if ((x > m_width) || (y >= m_height))
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
				pClrPixel(i,y0);
				++y0;
				--h0;
			}
		} while (h0 > 0);
	}
	return 0;
}


int SSD130X::writeHex(uint8_t h, bool comma)
{
	log_dbug(TAG,"writeHex %x",h);
	char c = h;
	if (h < 10)
		c += '0';
	else
		c += 'A' - 10;
	drawChar(c);
	if (comma)
		drawChar('.');
	return 0;
}


