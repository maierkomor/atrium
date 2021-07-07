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

#include "display.h"
#include "log.h"


static const char TAG[] = "disp";


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


TextDisplay *TextDisplay::Instance = 0;


void TextDisplay::initOK()
{
	if (Instance) {
		TextDisplay *i = Instance;
		while (i->m_next)
			i = i->m_next;
		i->m_next = this;
	} else {
		Instance = this;
	}
}


SegmentDisplay::SegmentDisplay(LedCluster *l, addrmode_t m, uint8_t maxx, uint8_t maxy)
: m_drv(l)
, m_addrmode(m)
, m_maxx(maxx)
, m_maxy(maxy)
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
	if (e_seg7) {
		if (char2seg7(c))
			return true;
		if (c == ' ')
			return true;
		if (c == '.')
			return true;
	} else if (e_seg14) {
		if (char2seg14(c))
			return true;
		if (c == ' ')
			return true;
		if (c == '.')
			return true;
	}
	return false;
}


int SegmentDisplay::setPos(uint8_t x, uint8_t y)
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


int SegmentDisplay::writeChar(char c, bool comma)
{
	log_dbug(TAG,"writeChar(%c,%d)",c,comma);
	if (m_addrmode == e_raw)
		return -1;
	if (c == '\n') {
		uint16_t y = m_pos / m_maxy;
		++y;
		if (y > m_maxy)
			y = 0;
		return setPos(0,y);
	}
	if (c == '\r') {
		uint16_t y = m_pos / m_maxy;
		return setPos(0,y);
	}
	if (m_addrmode == e_seg14) {
		uint16_t d = char2seg14(c);
		if (d == 0) {
			if (c == ' ') {
				writeBin(0);
				return writeBin(comma?0x80:0);
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

int SegmentDisplay::write(const char *s, int n)
{
	log_dbug(TAG,"write('%s')",s);
	while (*s && (n != 0)) {
		bool comma = s[1] == '.';
		if (writeChar(*s,comma))
			return 1;
		if (comma)
			++s;
		++s;
		--n;
	}
	return 0;
}
