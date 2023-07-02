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

#ifndef DISPLAY_H
#define DISPLAY_H

#include "ledcluster.h"
#include "fonts_generic.h"

class MatrixDisplay;
class SegmentDisplay;

typedef enum color_e {
	BLACK = 1, WHITE, BLUE, RED, GREEN, CYAN, YELLOW, MAGENTA	//, PURPLE
} color_t;

typedef enum colorspace_e
	{ cs_mono, cs_map8
	, cs_rgb15, cs_rgb16, cs_rgb18, cs_rgb24
	, cs_bgr15, cs_bgr16, cs_bgr18, cs_bgr24
} colorspace_t;

#define COLOR_DEFAULT -1
#define COLOR_NONE -2


color_t color_get(const char *);

struct TextDisplay
{
	virtual MatrixDisplay *toMatrixDisplay()
	{ return 0; }

	virtual SegmentDisplay *toSegmentDisplay()
	{ return 0; }

	virtual void clear()
	{ }

	// can display specific char?
	virtual bool hasChar(char c) const
	{ return false; }

	virtual int setBlink(bool)
	{ return -1; }

	virtual int setCursor(bool)
	{ return -1; }

	virtual int setPos(uint16_t x, uint16_t y = 0);

	// may change after changing the font
	virtual uint16_t charsPerLine() const
	{ return 0; }

	virtual uint16_t charWidth(char c) const
	{ return 1; }

	// may change after changing the font
	virtual uint16_t numLines() const
	{ return 0; }

	virtual int setOn(bool on)
	{ return -1; }

	static TextDisplay *getFirst()
	{ return Instance; }

	uint16_t maxX() const
	{ return m_width; }

	uint16_t maxY() const
	{ return m_height; }

//	virtual int writeBin(uint8_t)
//	{ return -1; }

	virtual int writeHex(uint8_t h, bool comma = false)
	{ return -1; }

	virtual void write(const char *txt, int n = -1)
	{ }

	// support for displaying a-z, A-Z?
	virtual bool hasAlpha() const
	{ return false; }

	virtual uint8_t maxBrightness() const
	{ return 1; }

	virtual int getBrightness() const
	{ return -1; }

	virtual int setBrightness(uint8_t)
	{ return -1; }

	virtual void flush()
	{ }

	virtual void clrEol()
	{ }

	virtual int setFont(unsigned)
	{ return -1; }

	virtual int setFont(const char *)
	{ return -1; }

	void initOK();

	protected:
	TextDisplay(uint16_t w, uint16_t h)
	: m_width(w)
	, m_height(h)
	{ }

	TextDisplay()
	{ }

	uint16_t m_width = 0, m_height = 0;
	uint16_t m_posx = 0, m_posy = 0;	// cursor position

	private:
	static TextDisplay *Instance;
};


struct MatrixDisplay : public TextDisplay
{
	explicit MatrixDisplay(colorspace_t cs)
	: m_colorspace(cs)
	, m_colfg(getColor(WHITE))
	, m_colbg(getColor(BLACK))
	{
	}

	MatrixDisplay *toMatrixDisplay() override
	{ return this; }

	/*
	int setXY(uint16_t x, uint16_t y)
	{
		if (x >= m_width)
			return -1;
		if (y >= m_height)
			return -1;
		m_posx = x;
		m_posy = y;
		return 0;
	}
	*/

	virtual int setFont(unsigned);
	virtual int setFont(const char *);

	uint16_t charWidth(char c) const override;
	uint16_t fontHeight() const;
	void clrEol() override;
	uint16_t charsPerLine() const override
	{ return m_width/8; }
	uint16_t numLines() const override
	{ return m_height/fontHeight(); }

	virtual void setPixel(uint16_t x, uint16_t y, int32_t color)
	{ }

	// col = -1: use default color (m_colfg/m_colbg)
	// col = -2 for bg: do not fill
	virtual void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col = -1);
	virtual void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int32_t col = -1);
	virtual void drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data, int32_t fg = -1, int32_t bg = -1);
	// without transparency
	virtual void drawPicture16(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);
	// with transparency (*data == -1) and 24bit color depth
	virtual void drawPicture32(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const int32_t *data);
	virtual void drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, int32_t col = -1);
	virtual void drawHLine(uint16_t x0, uint16_t y0, uint16_t len, int32_t col = -1);
	virtual void drawVLine(uint16_t x0, uint16_t y0, uint16_t len, int32_t col = -1);
	virtual unsigned drawText(uint16_t x, uint16_t y, const char *txt, int n = -1, int32_t fg = -1, int32_t bg = -1);
	virtual unsigned drawChar(uint16_t x, uint16_t y, char c, int32_t fg, int32_t bg);

	virtual int setInvert(bool)
	{ return -1; }

	virtual int32_t getColor(color_t) const;
	virtual int32_t setFgColor(color_t);
	virtual int32_t setBgColor(color_t);

	void setFgColorVal(int32_t v)
	{
		if ((v >= 0) && (v <= UINT16_MAX))
			m_colfg = v;
	}

	void setBgColorVal(int32_t v)
	{
		if ((v >= 0) && (v <= UINT16_MAX))
			m_colbg = v;
	}

//	virtual int setContrast(uint8_t contrast)
//	{ return -1; }

	void flush() override
	{ }

	void clear() override;
	void write(const char *txt, int n = -1) override;

#if 0	// TODO
	void setClipping(uint16_t xl, uint16_t xh, uint16_t yl, uint16_t yh)
	{ m_clxl = xl; m_clxh = xh; m_clyl = yl; m_clyh = yh; }
#endif

	protected:
//	int writeChar(uint16_t x, uint16_t y, char c);

#if 0	// TODO
	// clip x/y-low/high
	uint16_t m_clxl = 0, m_clxh = 0xffff, m_clyl = 0, m_clyh = 0xffff;
#endif
	fontid_t m_font = font_native;
	colorspace_t m_colorspace;
	int32_t m_colfg, m_colbg;
};


struct SegmentDisplay : public TextDisplay
{
	typedef enum { e_raw = 0, e_seg7 = 1, e_seg14 = 2 } addrmode_t;

	SegmentDisplay(LedCluster *, addrmode_t m, uint8_t maxx, uint8_t maxy = 1);

	SegmentDisplay *toSegmentDisplay() override
	{ return this; }

	//int writeHex(uint8_t d, bool comma = false);
	int writeChar(char, bool = false);
	void write(const char *txt, int n = -1) override;
	void clear() override
	{ m_drv->clear(); }

	int setOn(bool on) override
	{ return m_drv->setOn(on); }

	int getBrightness() const override
	{ return m_drv->getDim(); }

	int setBrightness(uint8_t d) override
	{ return m_drv->setDim(d); }

	// X=0,Y=0: upper left
	int setPos(uint16_t x, uint16_t y = 0) override;

	/*
	// characters per line
	uint16_t charsPerLine() const override
	{ return m_width; }

	// number of lines
	uint16_t numLines() const override
	{ return m_height; }
	*/

	// maximum brightness
	uint8_t maxBrightness() const override
	{ return m_drv->maxDim(); }

	bool hasAlpha() const override
	{ return (m_addrmode == e_seg14); }

	bool hasChar(char) const override;

	protected:
	static uint16_t char2seg7(char c);
	static uint16_t char2seg14(char c);
	int writeBin(uint8_t);

	LedCluster *m_drv;
	addrmode_t m_addrmode;
};


#endif
