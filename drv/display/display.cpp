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

#include "display.h"
#include "log.h"
#include "profiling.h"

#include <math.h>


#define TAG MODULE_DISP


static const uint8_t Circle[] = {
	0b1000,
	0b10000,
	0b10,
	0b10000000,
	0b1000000,
	0b100000,
	0b1,
	0b100,
};


// digits for 7-segment displays
static const uint8_t HexTable7[] = {
	0b01111110,
	0b00110000,
	0b01101101,		// 2
	0b01111001,		// 3
	0b00110011,		// 4
	0b01011011,		// 5
	0b01011111,		// 6
	0b01110000,		// 7
	0b01111111,		// 8
	0b01111011,		// 9
	0b01110111,		// A
	0b00011111,		// b
	0b01001110,		// C
	0b00111101,		// d
	0b01001111,		// E
	0b01000111,		// F
};


// digits for 14-segment displays
static const uint16_t DecTable14[] = {
	0b110000111111,		// 0
	0b00000110,		// 1
	0b11011011,		// 2
	0b11001111,		// 3
	0b11100110,		// 4
	0b11101101,		// 5
	0b00011111101,		// 6
	0b1010000000001,	// 7
	0b11111111,		// 8
	0b11101111,		// 9
};


// upper case alphabet for 14-segment displays
static const uint16_t AlphaTable[] = {
	0b11110111,		// A
	0b0001001010001111,	// B
	0b00111001,		// C
	0b0001001000001111,	// D
	0b11111001,		// E
	0b01110001,		// F
	0b10111101,		// G
	0b11110110,		// H
	0b0001001000001001,	// I
	0b0000000000011111,	// J
	0b0010010001110000,	// K
	0b00111000,		// L
	0b0000010100110110,	// M
	0b0010000100110110,	// N
	0b00111111,		// O
	0b11110011,		// P
	0b0010000000111111,	// Q
	0b0010000011110011,	// R
	0b0000000110001101,	// S
	0b0001001000000001,	// T
	0b0000000000111110,	// U
	0b110000110000,		// V
	0b0010100000110110,	// W
	0b0010110100000000,	// X
	0b0001010100000000,	// Y
	0b110000001001,		// Z
};


// 7-segment progress circle
static const uint8_t Circle7[] = {
	0b1000,
	0b10000,
	0b10,
	0b10000000,
	0b1000000,
	0b100000,
	0b1,
	0b100,
};


// 7-segment progress circle
static const uint8_t Circle14[] = {
	0b1000,
	0b10000,
	0b10,
	0b10000000,
	0b1000000,
	0b100000,
	0b1,
	0b100,
};


static struct { const char *name; color_t color; } Colors[] = {
	{ "black",	BLACK },
	{ "white",	WHITE },
	{ "blue",	BLUE },
	{ "red",	RED },
	{ "green",	GREEN },
	{ "cyan",	CYAN },
	{ "magenta",	MAGENTA },
	{ "yellow",	YELLOW },
};


TextDisplay *TextDisplay::Instance = 0;


color_t color_get(const char *n)
{
	for (const auto &c : Colors) {
		if (0 == strcasecmp(n,c.name))
			return c.color;
	}
	return BLACK;
}


void TextDisplay::initOK()
{
	if (Instance) {
		log_error(TAG,"only one display is supported");
	} else {
		Instance = this;
	}
}


int TextDisplay::setPos(uint16_t x, uint16_t y)
{
	log_dbug(TAG,"setPos(%u,%u)",x,y);
	if ((x < m_width) && (y < m_height)) {
		m_posx = x;
		m_posy = y;
		return 0;
	}
	return -1;
}


SegmentDisplay::SegmentDisplay(LedCluster *l, addrmode_t m, uint8_t maxx, uint8_t maxy)
: TextDisplay(maxx,maxy)
, m_drv(l)
, m_addrmode(m)
{
	l->setNumDigits((maxx+1)*(maxy+1));
	initOK();
	log_dbug(TAG,"segment display on %s",l->drvName());
}


uint16_t SegmentDisplay::char2seg7(char c)
{
	uint16_t v;
	if ((c >= '0') && (c <= '9'))
		v = HexTable7[c-'0'];
	else if ((c >= 'a') && (c <= 'f'))
		v = HexTable7[c-'a'+10];
	else if ((c >= 'A') && (c <= 'F'))
		v = HexTable7[c-'A'+10];
	else if ((c > 0) && (c < 8))
		v = (1 << c);
	else switch (c) {
		case '-':
			v = 0b0000001;
			break;
		case '"':
			v = 0b0100010;
			break;
		case '#':
			v = 0b01100011;
			break;
		case '\'':
			v = 0b0000010;
			break;
		case 'A':
			v = 0b01110111;
			break;
		case 'b':
			v = 0b00011111;
			break;
		case 'c':
			v = 0b00001101;
			break;
		case 'C':
			v = 0b01001110;
			break;
		case 'd':
			v = 0b00111101;
			break;
		case 'E':
			v = 0b01001111;
			break;
		case 'F':
			v = 0b01000111;
			break;
		case 'h':
			v = 0b00010111;
			break;
		case 'H':
			v = 0b00110111;
			break;
		case 'L':
			v = 0b00001110;
			break;
		case 'o':
			v = 0b00110101;
			break;
		case 'P':
			v = 0b01100111;
			break;
		case 'U':
			v = 0b01111110;
			break;
		case 'u':
			v = 0b00011100;
			break;
		case 'Y':
			v = 0b00110011;
			break;
		default:
			v = 0;
	}
	return v;
}


uint16_t SegmentDisplay::char2seg14(char c)
{
	uint16_t v;
	if ((c >= 'A') && (c <= 'Z'))
		v = AlphaTable[c-'A'];
	else if ((c >= 'a') && (c <= 'z'))
		v = AlphaTable[c-'a'];
	else if ((c >= '0') && (c <= '9'))
		v = DecTable14[c-'0'];
	else if (c == '*')
		v = 0b11111111000000;
	else if (c == '/')
		v = 0b00110000000000;
	else if (c == '+')
		v = 0b01001011000000;
	else if (c == '-')
		v = 0b00000011000000;
	else if (c == '%')
		v = 0b10110111100100;
	else if (c == '#')
		v = 0b11100011;
	else if (c == ' ')
		v = 0b00000000000000;
	else if ((c >= 1) && (c <= 8))
		v = (uint16_t)Circle[c-1] << 6;
	else
		v = 0;
	return v;
}


bool SegmentDisplay::hasChar(char c) const
{
	if (m_addrmode == e_seg7) {
		if (char2seg7(c))
			return true;
		if (c == ' ')
			return true;
		if (c == '.')
			return true;
	} else if (m_addrmode == e_seg14) {
		if (char2seg14(c))
			return true;
		if (c == ' ')
			return true;
		if (c == '.')
			return true;
	}
	return false;
}


int SegmentDisplay::setPos(uint16_t x, uint16_t y)
{
	log_dbug(TAG,"setPos(%u,%u)",x,y);
//	if ((x >= m_maxx) || (y >= m_maxy))
//		return 1;
//	m_pos = y * m_maxx + x;
//	return m_drv->setOffset(m_pos);
	return m_drv->setPos(x,y);
}


int SegmentDisplay::writeBin(uint8_t v)
{
	return m_drv->write(v);
}


/*
int SegmentDisplay::writeHex(uint8_t v, bool comma)
{
	log_dbug(TAG,"writeHex(%x,%d)",v,comma);
	if ((v > 15) || (m_addrmode == e_raw))
		return 1;
	if (m_addrmode == e_seg14) {
		uint16_t b;
		if (v < 10)
			b = DecTable14[v];
		else
			b = AlphaTable[v-10];
		if (comma)
			b |= 0x4000;
		if (m_drv->write(b&0xff))
			return 1;
		return m_drv->write(b>>8);
	} else if (m_addrmode == e_seg7) {
		uint8_t x = HexTable7[v];
		if (comma)
			x |= 0x80;
		return m_drv->write(x);
	}
	return -1;
}
*/


int SegmentDisplay::writeChar(char c, bool comma)
{
	log_dbug(TAG,"writeChar('%c',%d) at %d/%d",c,comma,m_posx,m_posy);
	if (m_addrmode == e_raw)
		return -1;
	if (c == '\n') {
		uint16_t y = m_posy + 1;
		if (y >= m_height)
			y = m_height - 1;
		m_posy = y;
		return 0;
	}
	if (c == '\r') {
		m_drv->write(c);
		m_posx = 0;
		return 0;
	}
	if (m_addrmode == e_seg14) {
		uint16_t d = char2seg14(c);
		if (d == 0) {
			if (c == ' ') {
				writeBin(0);
				writeBin(comma?0x80:0);
			}
			return 0;	// no error to print rest of string
		}
		if (comma)
			d |= 0x4000;
		if (writeBin(d&0xff))
			return 1;
		return writeBin(d>>8);
	} else if (m_addrmode == e_seg7) {
		switch (c) {
		case ' ':
			writeBin(0b0);
			break;
		case '%':
			writeBin(0b01100011);
			writeBin(0b00011101);
			break;
		default:
			if (uint8_t d = char2seg7(c)) {
				if (comma)
					d |= 0x80;
				return writeBin(d);
			} else {
				log_dbug(TAG,"char '%c' not found",c);
				return 1;
			}
		}
		return 0;
	}
	return 1;
}


void SegmentDisplay::write(const char *s, int n)
{
	log_dbug(TAG,"write('%s',%d)",s,n);
	while (*s && (n != 0)) {
		log_dbug(TAG,"write('%s',%d) ::",s,n);
		bool comma = (s[1] == '.') || ((s[1] == ':') && !hasChar(':'));
		writeChar(*s,comma);
		if (comma)
			++s;
		++s;
		--n;
	}
}


uint16_t MatrixDisplay::fontHeight() const
{
	return m_font->yAdvance;
}


void MatrixDisplay::clrEol()
{
	fillRect(m_posx,m_posy,m_width-m_posx,fontHeight(),m_colbg);
}


void MatrixDisplay::drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg, int32_t bg)
{
	log_dbug(TAG,"drawBitmap(%u,%u,%u,%u,%d,%d)",x,y,w,h,fg,bg);
	if (fg == -1)
		fg = m_colfg;
	if (bg == -1)
		bg = m_colbg;
	const uint8_t *e = data + w*h;
	if (fg != -2) {
		unsigned idx = 0;
		uint8_t b = 0;
		const uint8_t *d = data;
		while (d != e) {
			if ((idx & 7) == 0) {
				b = *d++;
				if (b == 0) {
					idx += 8;
					continue;
				}
			}
			if (b&0x80)
				setPixel(x+idx%w,y+idx/w,fg);
			b<<=1;
			++idx;
		}
	}
	if (bg != -2) {
		unsigned idx = 0;
		uint8_t b = 0;
		const uint8_t *d = data;
		while (d != e) {
			if ((idx & 7) == 0) {
				b = *d++;
				if (b == 0xff) {
					idx += 8;
					continue;
				}
			}
			if ((b&0x80) == 0)
				setPixel(x+idx%w,y+idx/w,bg);
			b<<=1;
			++idx;
		}
	}
}


uint8_t charToGlyph(char c)
{
	switch ((unsigned char) c) {
	case '\r':
		return 0;
	case '\n':
		return 0;
	case 176:	// '°'
		return 127;
	case 196:	// 'Ä'
		return 129;
	case 220:	// 'Ü'
		return 133;
	case 214:	// 'Ö'
		return 131;
	case 223:	// 'ß'
		return 134;
	case 228:	// 'ä'
		return 128;
	case 246:	// 'ö'
		return 130;
	case 252:	// 'ü'
		return 132;
	default:
		return c;
	}
}


uint16_t MatrixDisplay::charWidth(char c) const
{
	c = charToGlyph(c);
	if (c >= m_font->first) {
		uint8_t ch = c - m_font->first;
		if (m_font->glyph != 0)
			return m_font->glyph[ch].xAdvance;
	}
	return 0;
}


void MatrixDisplay::clear()
{
	log_dbug(TAG,"clear");
	m_posx = 0;
	m_posy = 0;
	fillRect(0,0,m_width,m_height,m_colbg);
}


unsigned MatrixDisplay::drawChar(uint16_t x, uint16_t y, char c, int32_t fg, int32_t bg)
{
	PROFILE_FUNCTION();
	c = charToGlyph(c);
	if ((c < m_font->first) || (c > m_font->last))
		return 0;
	if (fg == -1)
		fg = m_colfg;
	if (bg == -1)
		bg = m_colbg;
	uint8_t ch = c - m_font->first;
	const uint8_t *data = m_font->RMbitmap + m_font->glyph[ch].bitmapOffset;
	uint8_t w = m_font->glyph[ch].width;
	uint8_t h = m_font->glyph[ch].height;
	int8_t dx = m_font->glyph[ch].xOffset;
	int8_t dy = m_font->glyph[ch].yOffset;
	uint8_t a = m_font->glyph[ch].xAdvance;
	log_dbug(TAG,"drawChar(%d,%d,'%c') = %u",x,y,c,a);
//	log_info(TAG,"%d/%d %+d/%+d, adv %u len %u",(int)w,(int)h,(int)dx,(int)dy,a,l);
	if (bg != -2)
		fillRect(x,y,a,m_font->yAdvance,bg);
	drawBitmap(x+dx,y+dy,w,h,data,fg,bg);
	return a;
}


unsigned MatrixDisplay::drawText(uint16_t x, uint16_t y, const char *txt, int n, int32_t fg, int32_t bg)
{
	if (n < 0)
		n = strlen(txt);
	log_dbug(TAG,"drawText(%d,%d,'%.*s',%d,%d)",x,y,n,txt,fg,bg);
	uint16_t a = 0, amax = 0;
	const char *e = txt + n;
	while (txt != e) {
		char c = *txt++;
		if (c == 0)
			break;
		if (c == '\n') {
			x = 0;
			y += m_font->yAdvance;
			if (y+m_font->yAdvance > m_height)
				break;
			if (a > amax)
				amax = a;
			a = 0;
		} else if (c == '\r') {
			if (a > amax)
				amax = a;
			a = 0;
			x = 0;
		} else if (c == 0xc2) {
			// first byte of a 2-byte utf-8
			// used for degree symbol \u00b0 i.e.
			// 0xc2 0xb0
			continue;
		} else {
			uint16_t cw = charWidth(c);
			if (x + cw > m_width)
				break;
			drawChar(x,y,c,fg,bg);
			a += cw;
			x += cw;
		}
	}
	return a > amax ? a : amax;
}


void MatrixDisplay::drawPicture16(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
	for (uint16_t i = x, xe = x+w; i < xe; ++i) {
		for (uint16_t j = y, ye = x+w; j < ye; ++j)
			setPixel(i,j,*data++);
	}
}


void MatrixDisplay::drawPicture32(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const int32_t *data)
{
	for (uint16_t i = x, xe = x+w; i < xe; ++i) {
		for (uint16_t j = y, ye = x+w; j < ye; ++j) {
			if (*data != -1)
				setPixel(i,j,*data);
			++data;
		}
	}
}


void MatrixDisplay::drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, int32_t col)
{
	if (col == -1)
		col = m_colfg;
	uint16_t x = x0;
	uint16_t y = y0;
	uint16_t dx = x1 - x0;
	uint16_t dy = y1 - y0;
	float d = 2 * dy - dx; // discriminator
    
	// Euclidean distance of point (x,y) from line (signed)
	float D = 0; 
    
	// Euclidean distance between points (x1, y1) and (x2, y2)
	float length = sqrtf((float)(dx * dx + dy * dy)); 
    
	float sin = dx / length;
	float cos = dy / length;
	while (x <= x1) {
//		IntensifyPixels(x, y - 1, D + cos);
//		IntensifyPixels(x, y, D);
//		IntensifyPixels(x, y + 1, D - cos);
		setPixel(x,y,col);
		++x;
		if (d <= 0) {
			D = D + sin;
			d = d + 2 * dy;
		} else {
			D = D + sin - cos;
			d = d + 2 * (dy - dx);
			++y;
		}
	}
}


void MatrixDisplay::drawHLine(uint16_t x, uint16_t y, uint16_t len, int32_t col)
{
	if (col == -1)
		col = m_colfg;
	if ((x >= m_width) || (y >= m_height))
		return;
	if ((x+len) > m_width)
	       len = m_width - x;	
	while (len) {
		setPixel(x,y,col);
		++x;
		--len;
	}
}


void MatrixDisplay::drawVLine(uint16_t x, uint16_t y, uint16_t len, int32_t col)
{
	if (col == -1)
		col = m_colfg;
	if ((x >= m_width) || (y >= m_height))
		return;
	if ((y+len) > m_height)
	       len = m_height - y;	
	while (len) {
		setPixel(x,y,col);
		++y;
		--len;
	}
}


void MatrixDisplay::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col)
{
	log_dbug(TAG,"drawRect(%u,%u,%u,%u,%x)",x,y,w,h,col);
	if (col == -1)
		col = m_colfg;
	drawHLine(x,y,w,col);
	drawHLine(x,y+h-1,w,col);
	drawVLine(x,y,h,col);
	drawVLine(x+w-1,y,h,col);
//	log_hex(TAG,m_disp,m_width*m_height/8,"frame");
}


void MatrixDisplay::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col)
{
	if (col == -1)
		col = m_colfg;
	if ((x >= m_width) || (y >= m_height))
		return;
	if ((x+w) > m_width)
		w = m_width - x;
	if ((y+h) > m_height)
		h = m_height - y;
	while (h) {
		drawHLine(x,y,w,col);
		++y;
		--h;
	}
}


static int32_t getColor16BGR(color_t c)
{
	switch (c) {
	case WHITE:	return 0xffff;
	case BLACK:	return 0x0000;
	case BLUE:	return 0xf800;
	case RED:	return 0x001f;
	case GREEN:	return 0x07e0;
	case YELLOW:	return 0xffe0;
	case CYAN:	return 0x07ff;
	case MAGENTA:	return 0xf81f;
	default:	return -1;
	}
}


static int32_t getColor18BGR(color_t c)
{
	switch (c) {
	case WHITE:	return 0xfcfcfc;
	case BLACK:	return 0x000000;
	case BLUE:	return 0xfc0000;
	case RED:	return 0x0000cf;
	case GREEN:	return 0x00fc00;
	case YELLOW:	return 0xfcfc00;
	case CYAN:	return 0x00fcfc;
	case MAGENTA:	return 0xfc00fc;
	default:	return -1;
	}
}


static int32_t getColor16RGB(color_t c)
{
	switch (c) {
	case WHITE:	return 0xffff;
	case BLACK:	return 0x0000;
	case BLUE:	return 0x001f;
	case RED:	return 0xf800;
	case GREEN:	return 0x07e0;
	case YELLOW:	return 0xffe0;
	case CYAN:	return 0x07ff;
	case MAGENTA:	return 0xf81f;
	default:	return -1;
	}
}


static int32_t getColor18RGB(color_t c)
{
	switch (c) {
	case WHITE:	return 0xfcfcfc;
	case BLACK:	return 0x000000;
	case BLUE:	return 0x0000fc;
	case RED:	return 0xfc0000;
	case GREEN:	return 0x00fc00;
	case YELLOW:	return 0xfcfc00;
	case CYAN:	return 0x00fcfc;
	case MAGENTA:	return 0xfc00fc;
	default:	return -1;
	}
}


int32_t MatrixDisplay::getColor(color_t c) const
{
	switch (m_colorspace) {
	case cs_mono:
		switch (c) {
		case WHITE:	return 0x1;
		case BLACK:	return 0x0;
		default:	return -1;
		}
	case cs_rgb16:
		return getColor16RGB(c);
	case cs_rgb18:
		return getColor18RGB(c);
	case cs_bgr16:
		return getColor16BGR(c);
	case cs_bgr18:
		return getColor18BGR(c);
	default:
		return -1;
	}
}


int32_t MatrixDisplay::setFgColor(color_t c)
{
	int32_t col = getColor(c);
	if (col != -1)
		m_colfg = col;
	return col;
}


int32_t MatrixDisplay::setBgColor(color_t c)
{
	int32_t col = getColor16RGB(c);
	if (col != -1)
		m_colfg = col;
	return col;
}


int MatrixDisplay::setFont(unsigned f)
{
	if (f < NumFonts) {
		m_font = Fonts+f;
		return 0;
	}
	return -1;
}


int MatrixDisplay::setFont(const char *fn)
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
	for (int i = 0; i < NumFonts; ++i) {
		if (0 == strcasecmp(Fonts[i].name,fn)) {
			m_font = Fonts+i;
			return 0;
		}
	}
	return -1;
}


int MatrixDisplay::setupOffScreen(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t bg)
{
	return -1;
}


void MatrixDisplay::commitOffScreen()
{
}


unsigned MatrixDisplay::textWidth(const char *t, int f)
{
	// uses row-major font
	if (f >= NumFonts)
		return 0;
	const Font *font;
	if (f == -1)
		font = m_font;
	else
		font = &Fonts[f];
	unsigned w = 0;
	unsigned s = font->first;
	unsigned e = font->last;
	while (char c = *t) {
		if ((c >= s) && (c <= e))
			w += font->glyph[c-s].xAdvance;
		++c;
	}
	return w;
}


void MatrixDisplay::write(const char *txt, int n)
{
	log_dbug(TAG,"write '%s' at %u/%u",txt,m_posx,m_posy);
	if (n < 0)
		n = strlen(txt);
	const char *e = txt + n;
	while (txt != e) {
		const char *at = txt;
		char c = *at;
		while (c && (c != '\n') && (c != '\r') && n) {
			++at;
			c = *at;
			--n;
		}
		if (at != txt) {
			m_posx += drawText(m_posx,m_posy,txt,at-txt,-1,-2);
			txt = at;
			if (m_posx >= m_width) {
				m_posx = m_width - 1;
				return;
			}
		}
		if (c == '\n') {
			uint16_t fh = m_font->yAdvance;
			m_posx = 0;
			if (m_posy + fh >= m_height) {
				m_posy = m_height -1;
				return;
			}
			m_posy += fh;
			++txt;
		} else if (c == '\r') {
			m_posx = 0;
			++txt;
		/*
		} else {
			unsigned w = drawChar(m_posx,m_posy,c,m_colfg,m_colbg);
			m_posx += w;
			if (m_posx >= m_width) {
				m_posx = m_width - 1;
				return;
			}
		*/
		}
	}
#if 0
	if (n < 0)
		n = strlen(txt);
	unsigned width = 0;
	for (unsigned x = 0; x < n; ++x) {
		width += charWidth(txt[x]);
	}
	if (m_posx + width > m_width)
		return -1;
	const Font *font = Fonts+(int)m_font;
	uint16_t height = font->yAdvance;
	uint16_t widht = 0;
	uint16_t x = m_posx;
	const char *e = txt + n;
	const char *at = txt;
	while (at != e) {
		char c = *at++;
		if ((c < font->first) || (c > font->last))
			continue;
		uint8_t ch = c - font->first;
		if (x + font->glyph[ch].xAdvance > m_width)
			break;
		width += font->glyph[ch].xAdvance;
	}
	e = at;
	at = txt;
	while (at != e) {
		// TODO
		const uint8_t *data = font->bitmap + font->glyph[ch].bitmapOffset;
	}
#endif
}

