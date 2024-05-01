/*
 *  Copyright (C) 2021-2024, Thomas Maier-Komor
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
#include "romfs.h"

#include <dirent.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAG MODULE_DISP

using namespace std;


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


const font_t *DefaultFonts[] = {
	Fonts+0,
	Fonts+1,
	Fonts+2,
	Fonts+3,
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


unsigned Font::getCharWidth(uint32_t ch) const
{
	if (const glyph_t *g = getGlyph(ch))
		return g->width + g->xOffset;
	return 0;
}


/*
 * obsolete, no UTF-* support
void Font::getTextDim(const char *str, uint16_t &W, int8_t &ymin, int8_t &ymax) const
{
	// blank upper margin not calculated
	int8_t hiy = INT8_MIN, loy = INT8_MAX;
	uint16_t w = 0;
	while (char c = *str) {
		const glyph_t *g = &glyph[c-first];
		w += g->width;
		w += g->xOffset;
		if (g->yOffset < loy)
			loy = g->yOffset;
		if (g->height+g->yOffset > hiy)
			hiy = g->height+g->yOffset;
		++str;
	}
	W = w;
	ymin = loy;
	ymax = hiy;
}
*/

unsigned Font::textWidth(const char *t, int l) const
{
	unsigned width = 0;
	if (-1 == l)
		l = strlen(t);
	bool err = false;
	const char *e = t + l;
	char c = *t++;
	while ((t <= e) && (0 != c)) {
		uint32_t w;
		if (0 == (c & 0x80)) {
			// single byte char
			w = c;
		} else if (0xc0 == (c & 0xe0)) {
			// two byte char
			w = ((uint32_t)(c & 0x1f)) << 6;
			c = *t++;
			if (0x80 != (c & 0xc0)) {
				err = true;
				break;
			}
			w |= c & 0x3f;
		} else if (0xe0 == (c & 0xf0)) {
			// three byte char
			w = ((uint32_t)(c & 0x0f)) << 12;
			c = *t++;
			if (0x80 != (c & 0xc0)) {
				err = true;
				break;
			}
			w |= ((uint32_t)(c & 0x3f)) << 6;
			c = *t++;
			if (0x80 != (c & 0xc0)) {
				err = true;
				break;
			}
			w |= (uint32_t)(c & 0x3f);
		} else if (0xf0 == (c & 0xf8)) {
			// four byte char
			w = ((uint32_t)(c & 0x7)) << 18;
			c = *t++;
			if (0x80 != (c & 0xc0)) {
				err = true;
				break;
			}
			w |= ((uint32_t)(c & 0x0f)) << 12;
			c = *t++;
			if (0x80 != (c & 0xc0)) {
				err = true;
				break;
			}
			w |= ((uint32_t)(c & 0x3f)) << 6;
			c = *t++;
			if (0x80 != (c & 0xc0)) {
				err = true;
				break;
			}
			w |= (uint32_t)(c & 0x3f);
		} else {
			log_warn(TAG,"UTF-8 encoding error");
			break;
		}
		if (const glyph_t *g = getGlyph(c))
			width += g->xAdvance;
		--l;
		c = *t++;
	}
	if (err)
		log_warn(TAG,"UTF-8 encoding error");
	return width;
}


const glyph_t *Font::getGlyph(uint32_t ch) const
{
	if ((ch >= first) && (ch <= last)) {
		return glyph+ch-first;
	}
	unsigned n = extra;
	const glyph_t *g = glyph+last-first+1;
	while (n) {
		if (g->iso8859 == ch)
			return g;
		++g;
		--n;
	}
	return 0;
}


void TextDisplay::initOK()
{
	if (Instance) {
		log_warn(TAG,"only 1 display is supported");
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


void MatrixDisplay::initFonts()
{
#if defined CONFIG_ROMFS && defined ESP32
	int f = romfs_num_entries();
	while (f) {
		--f;
		const char *n = romfs_name(f);
		if (((pxf_rowmjr == pixelFormat()) && (0 == strendcmp(n,".af1")))
			|| ((pxf_bytecolmjr == pixelFormat()) && (0 == strendcmp(n,".af2")))
			|| ((pxf_colmjr == pixelFormat()) && (0 == strendcmp(n,".af3")))
			|| (0 == strendcasecmp(n,".afn"))) {
			const uint8_t *data = (const uint8_t *)romfs_mmap(f);
			log_info(TAG,"romfs font file %s",n);
			addFont(data,romfs_size_fd(f));
		}
	}
#endif
	if (DIR *d = opendir("/flash")) {
		struct dirent *e = readdir(d);
		while (e) {
			if (((pxf_rowmjr == pixelFormat()) && (0 == strendcasecmp(e->d_name,".af1")))
				|| ((pxf_bytecolmjr == pixelFormat()) && (0 == strendcasecmp(e->d_name,".af2")))
				|| ((pxf_colmjr == pixelFormat()) && (0 == strendcasecmp(e->d_name,".af3")))
				|| (0 == strendcasecmp(e->d_name,".afn"))) {
				size_t l = strlen(e->d_name);
				char path[8+l] = "/flash/";
				strcpy(path+7,e->d_name);
				loadFont(path);
			}
			e = readdir(d);
		}
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


static int32_t grayScale(int32_t col, uint8_t a)
{
	if (a == 0)
		return 0;
	if (a == 0xff)
		return col;
	uint8_t r = col & 0xff, g = (col >> 8) & 0xff, b = (col >> 16) & 0xff;
	while ((a & 0x80) == 0) {
		r >>= 1;
		g >>= 1;
		b >>= 1;
		a <<= 1;
	}	
	return (b << 16) | (g << 8) | r;
}


void MatrixDisplay::drawPgm(uint16_t x0, uint16_t y, uint16_t w, uint16_t h, uint8_t *data, int32_t fg)
{
	log_dbug(TAG,"draw pgm %ux%u@%u,%u",w,h,x0,y);
	uint16_t ye = y+h, xe = x0+w;
	while (y < ye) {
		for (uint16_t x = x0; x < xe; ++x) {
			uint8_t g = *data;
			if (0xff == g)
				setPixel(x,y,fg);
			else if (g)
				setPixel(x,y,grayScale(fg,g));
			++data;
		}
		++y;
	}
}


void MatrixDisplay::drawPbm(uint16_t x0, uint16_t y, uint16_t w, uint16_t h, uint8_t *data, int32_t fg)
{
	log_dbug(TAG,"draw pbm %ux%u@%u,%u",w,h,x0,y);
	uint8_t byte = *data;
	unsigned off = 0;
	uint16_t ye = y+h;
	while (y < ye) {
		for (uint16_t x = x0; x < x0+w; ++x) {
			if (byte&0x80)
				setPixel(x,y,fg);
			byte <<= 1;
			++off;
			if ((off & 7) == 0) {
				++data;
				byte = *data;
			}
		}
		if (off&7) {
			++data;
			byte = *data;
			off = 0;
		}
		++y;
	}
	//drawBitmap(x0,y,w,h,data,fg,-1);
}


void MatrixDisplay::drawPpm(uint16_t x0, uint16_t y, uint16_t w, uint16_t h, uint8_t *data)
{
	log_dbug(TAG,"draw ppm %ux%u@%u,%u",w,h,x0,y);
	uint16_t ye = y+h;
	while (y < ye) {
		for (uint16_t x = x0; x < x0+w; ++x) {
			uint32_t col = *data++;
			col |= (*data++) << 8;
			col |= (*data++) << 16;
			setPixel(x,y,rgb24_to_native(col));
		}
		++y;
	}
}


// OLED bitmap with 8 bit vertical, arranged horizontal
void MatrixDisplay::drawObm(uint16_t x0, uint16_t y, uint16_t w, uint16_t h, uint8_t *data, int32_t fg)
{
	uint8_t byte = *data;
	unsigned off = 0;
	uint16_t ye = y+h;
	while (y < ye) {
		for (uint16_t x = x0; x < x0+w; ++x) {
			if (byte&1)
				setPixel(x,y,fg);
			byte >>= 1;
			++off;
			if ((off & 7) == 0)
				byte = *data++;
		}
		y += 8;
	}
}


void MatrixDisplay::drawIcon(uint16_t x0, uint16_t y0, const char *fn, int32_t fg)
{
	if (Image *i = openIcon(fn)) {
		log_dbug(TAG,"draw icon %s",fn);
		switch (i->type) {
		case img_pbm:
			drawPbm(x0,y0,i->w,i->h,i->data,fg);
			break;
		case img_pgm:
			drawPgm(x0,y0,i->w,i->h,i->data,fg);
			break;
		case img_ppm:
			drawPpm(x0,y0,i->w,i->h,i->data);
			break;
		case img_obm:
			drawObm(x0,y0,i->w,i->h,i->data,fg);
			break;
		default:
			return;
		}
	}
}


Image *MatrixDisplay::importImage(const char *fn, uint8_t *data, size_t s)
{
	if (data[0] == 'P') {
		unsigned typ, w, h, n0, depth = 0, n1 = 0;
		int c = sscanf((char*)data+1,"%u%u%u%n%u%n",&typ,&w,&h,&n0,&depth,&n1);
		if (3 > c) {
			log_warn(TAG,"invalid format of %s",fn);
			return 0;
		}
		Image i;
		i.w = w;
		i.h = h;
		if (typ == 4) {
			log_dbug(TAG,"%s is PBM",fn);
			if ((w*h) > ((s-2-n0)<<3)) {
				log_warn(TAG,"data in %s is truncated",fn);
				return 0;
			}
			i.data = data+n0+2;
			i.type = img_pbm;
		} else if (typ == 9) {
			log_dbug(TAG,"%s is OBM",fn);
			i.data = data+2+n0;
			i.type = img_obm;
		} else if (4 > c) {
			log_warn(TAG,"invalid format of %s",fn);
			return 0;
		} else if (typ == 5) {
			if (depth != 255) {
				log_warn(TAG,"unsupported color depth");
				return 0;
			}
			if (w*h > s-2-n1) {
				log_warn(TAG,"data in %s is truncated",fn);
				return 0;
			}
			i.data = data+s-(w*h);
			log_dbug(TAG,"%s is PGM",fn);
			i.type = img_pgm;
		} else if (typ == 6) {
			if (depth != 255) {
				log_warn(TAG,"unsupported color depth");
				return 0;
			}
			if (w*h*3 > s-2-n1) {
				log_warn(TAG,"data in %s is truncated",fn);
				return 0;
			}
			i.data = data+s-(w*h*3);
			log_dbug(TAG,"%s is PPM",fn);
			i.type = img_ppm;
		} else {
			log_warn(TAG,"unsupported format of %s",fn);
			return 0;
		}
		auto x = m_images.insert(make_pair(strdup(fn),i));
		return &x.first->second;
	}
	return 0;
}


Image *MatrixDisplay::openIcon(const char *fn)
{
	auto x = m_images.find(fn);
	if (x != m_images.end())
		return &x->second;
#if defined CONFIG_ROMFS && defined ESP32
	int rfd = romfs_open(fn);
	if (rfd != -1) {
		if (uint8_t *data = (uint8_t *) romfs_mmap(rfd))
			return importImage(fn,data,romfs_size_fd(rfd));
	}
#endif
	int fd = open(fn,O_RDONLY);
	if (fd == -1) {
		log_warn(TAG,"unable to open %s",fn);
		return 0;
	}
	struct stat st;
	if (-1 == fstat(fd,&st)) {
		log_warn(TAG,"failed to stat %s",fn);
	} else if (uint8_t *data = (uint8_t *) malloc(st.st_size)) {
		int n = read(fd,data,st.st_size);
		if (n == st.st_size) {
			close(fd);
			return importImage(fn,data,st.st_size);
		}
		log_warn(TAG,"failed to read %s",fn);
	} else {
		log_warn(TAG,"unable alloc for %s",fn);
	}
	close(fd);
	return 0;
}


uint16_t MatrixDisplay::charWidth(uint32_t c) const
{
	if (const glyph_t *g = m_font->getGlyph(c))
		return g->xAdvance;
	log_warn(TAG,"unknown glyph 0x%x",c);
	return 0;
}


void MatrixDisplay::clear()
{
	log_dbug(TAG,"clear");
	m_posx = 0;
	m_posy = 0;
	fillRect(0,0,m_width,m_height,m_colbg);
}


unsigned MatrixDisplay::drawChar(uint16_t x, uint16_t y, uint32_t c, int32_t fg, int32_t bg)
{
	PROFILE_FUNCTION();
	if (fg == -1)
		fg = m_colfg;
	if (bg == -1)
		bg = m_colbg;
	const glyph_t *g = m_font->getGlyph(c);
	if (0 == g)
		return 0;
	const uint8_t *data = m_font->RMbitmap + g->bitmapOffset;
	log_dbug(TAG,"drawChar(%d,%d,'%c'(0x%x)) = %u",x,y,(uint8_t)c,c,g->xAdvance);
	if (bg != -2) {
		fillRect(x,y,g->xAdvance,m_font->yAdvance,bg);
	}
	y += m_font->blOff;
	drawBitmap(x+g->xOffset,y+g->yOffset,g->width,g->height,data,fg,bg);
	return g->xAdvance;
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
		uint32_t w;
		if (c == '\n') {
			x = 0;
			y += m_font->yAdvance;
			if (y+m_font->yAdvance > m_height)
				break;
			if (a > amax)
				amax = a;
			a = 0;
			continue;
		} else if (c == '\r') {
			if (a > amax)
				amax = a;
			a = 0;
			x = 0;
			continue;
		} else if (0 == (c & 0x80)) {
			w = c;
		} else if (0xc0 == (c & 0xe0)) {
			// UTF-8 2-byte sequence signature
			// used for degree symbol \u00b0 i.e.
			// 0xc2 0xb0
			w = ((uint32_t)(c & 0x1f)) << 6;
			c = *txt++;
			if (0x80 != (c & 0xc0))	// 2nd byte requirement
				break;
			w |= c & 0x3f;
		} else if (0xe0 == (c & 0xf0)) {
			// UTF-8 3-byte sequence signature
			w = ((uint32_t)(c & 0xf)) << 12;
			c = *txt++;
			if (0x80 != (c & 0xc0))	// 2nd byte requirement
				break;
			w |= (c & 0x3f) << 6;
			c = *txt++;
			if (0x80 != (c & 0xc0))	// 3rd byte requirement
				break;
			w |= (c & 0x3f);
		} else if (0xf0 == (c & 0xf8)) {
			// UTF-8 4-byte sequence signature
			w = 0x100000 | (((uint32_t)(c & 0x7)) << 18);
			if (0x80 != (c & 0xc0))	// 2nd byte requirement
				break;
			w |= (c & 0x3f) << 12;
			c = *txt++;
			if (0x80 != (c & 0xc0))	// 3rd byte requirement
				break;
			w |= (c & 0x3f) << 6;
			if (0x80 != (c & 0xc0))	// 4th byte requirement
				break;
			w |= (c & 0x3f);
		} else {
			// invalid code sequence
			break;
		}
		uint16_t cw = charWidth(w);
		//log_dbug(TAG,"char 0x%02x: width %u",c,cw);
		if (x + cw > m_width)
			break;
		drawChar(x,y,w,fg,bg);
		a += cw;
		x += cw;
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


const Font *MatrixDisplay::getFont(int f) const
{
	if (f < 0) {
		f = -f - 1;
		if (f < sizeof(DefaultFonts)/sizeof(DefaultFonts[0]))
			return DefaultFonts[f];
	} else if (f < NumFonts) {
		return &Fonts[f];
	} else if (f - NumFonts < m_xfonts.size()) {
		return &m_xfonts[f-NumFonts];
	}
	return 0;
}


const Font *MatrixDisplay::setFont(int id)
{
	const Font *f = getFont(id);
	if (f)
		m_font = f;
	return f;
}


void MatrixDisplay::addFont(const uint8_t *data, size_t s)
{
	FontHdr *hdr = (FontHdr *)data;
	if (memcmp(hdr->magic,"Afnt",4)) {
		log_warn(TAG,"invalid font magic");
		return;
	}
	switch (pixelFormat()) {
	case pxf_invalid:
		return;
	case pxf_rowmjr:
		if (0 == hdr->offRM)
			return;
		break;
	case pxf_bytecolmjr:
		if (0 == hdr->offBCM)
			return;
		break;
	default:
		log_warn(TAG,"unspported pixel format");
		return;
	}
	Font f;
	if (0 != hdr->offRM)
		f.RMbitmap = data + hdr->offRM;
	else
		f.RMbitmap = 0;
	if (0 != hdr->offBCM)
		f.BCMbitmap = data + hdr->offBCM;
	else
		f.BCMbitmap = 0;
	f.glyph = (const glyph_t *)(data + hdr->offGlyph);
	f.first = hdr->first;
	f.last = hdr->last;
	f.extra = hdr->extra;
	// TODO: sanyity/security checks
//	log_dbug(TAG,"first 0x%x, last 0x%x, extra %u",f.first,f.last,f.extra);
	f.yAdvance = hdr->yAdv;
	f.name = hdr->name;
	int8_t miny = INT8_MAX;
	int8_t maxy = INT8_MIN, maxw = 0;
	unsigned n = f.last-f.first;
	for (unsigned x = 0; x <= n; ++x) {
		const glyph_t *g = f.glyph+x;
		if (g->yOffset < miny)
			miny = g->yOffset;
		if (g->xOffset+g->width > maxw)
			maxw = g->xOffset + g->width;
		if (g->yOffset + g->height > maxy)
			maxy = g->yOffset + g->height;
		if (g->xAdvance > maxw)
			maxw = g->xAdvance;
	}
	f.minY = miny;
	f.maxY = maxy;
	f.maxW = maxw;
	if (('A' >= f.first) && ('A' <= f.last))
		f.blOff = f.glyph['A'-f.first].height;
	else if (('0' >= f.first) && ('0' <= f.last))
		f.blOff = f.glyph['0'-f.first].height;
	else
		f.blOff = 0;
	log_info(TAG,"adding font %s: miny=%d, maxy=%d, maxw=%d bloff=%d",f.name,miny,maxy,maxw,f.blOff);
	for (Font &x : m_xfonts) {
		if (0 == strcmp(x.name,f.name)) {
			log_info(TAG,"updating font %s",f.name);
			if (0 == x.RMbitmap)
				x.RMbitmap = f.RMbitmap;
			if (0 == x.BCMbitmap)
				x.BCMbitmap = f.BCMbitmap;
			return;
		}
	}
	m_xfonts.push_back(f);
}


void MatrixDisplay::loadFont(const char *fn)
{
	log_info(TAG,"load font file %s",fn);
	int fd = open(fn,O_RDONLY);
	if (fd == -1) {
		log_warn(TAG,"open %s: %s",fn,strerror(errno));
		return;
	}
	struct stat st;
	if (-1 == fstat(fd,&st)) {
		log_warn(TAG,"stat %s: error",fn);
	} else if (uint8_t *data = (uint8_t *) heap_caps_malloc(st.st_size,MALLOC_CAP_SPIRAM)) {
		int n = read(fd,data,st.st_size);
		if (n == st.st_size) {
			addFont(data,st.st_size);
		} else {
			log_warn(TAG,"%s: read error",fn);
		}
	} else {
		log_warn(TAG,"%s: alloc failed",fn);
	}
	close(fd);

}


const Font *MatrixDisplay::getFont(const char *fn) const
{
	for (const auto &f : m_xfonts) {
		log_dbug(TAG,"font %s",f.name);
		if (0 == strcasecmp(fn,f.name)) {
			log_dbug(TAG,"set xfont %s",fn);
			return &f;
		}
	}
	for (int i = 0; i < NumFonts; ++i) {
		if (0 == strcasecmp(Fonts[i].name,fn)) {
			log_dbug(TAG,"set font %s",fn);
			return &Fonts[i];
		}
	}
	log_dbug(TAG,"unknown font %s",fn);
	return 0;
}


const Font *MatrixDisplay::setFont(const char *fn)
{
	if (const Font *f = getFont(fn)) {
		m_font = f;
		return f;
	}
	log_dbug(TAG,"font %s not found",fn);
	return 0;
}


void MatrixDisplay::setFont(int x, const Font *f)
{
	if (x >= 0)
		return;
	x = -x - 1;
	if ((x < sizeof(DefaultFonts)/sizeof(DefaultFonts[0])) && (0 != f))
		DefaultFonts[x] = f;
}


int MatrixDisplay::setupOffScreen(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t bg)
{
	return -1;
}


void MatrixDisplay::commitOffScreen()
{
}


unsigned MatrixDisplay::textWidth(const char *t, int l, int f) const
{
	// uses row-major font
	const Font *font;
	if (0 == f)	// default font
		font = m_font;
	else if (f < 0)
		font = &Fonts[-f-1];
	else if (f <= m_xfonts.size())
		font = &m_xfonts[f-1];
	else
		return 0;
	return font->textWidth(t,l);
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
		}
	}
}

