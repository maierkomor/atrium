/*
 *  Copyright (C) 2024, Thomas Maier-Komor
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

#ifndef FONT_H_
#define FONT_H_

#include <stdint.h>

/*
/// Font data stored PER GLYPH
typedef struct {
  uint16_t bitmapOffset; ///< Pointer into Font->bitmap
  uint8_t width;         ///< Bitmap dimensions in pixels
  uint8_t height;        ///< Bitmap dimensions in pixels
  uint8_t xAdvance;      ///< Distance to advance cursor (x axis)
  int8_t xOffset;        ///< X dist from cursor pos to UL corner
  int8_t yOffset;        ///< Y dist from cursor pos to UL corner
} GFXglyph;
*/

typedef struct Glyph {
  uint16_t bitmapOffset; ///< Pointer into Font->bitmap
  uint8_t width;         ///< Bitmap dimensions in pixels
  uint8_t height;        ///< Bitmap dimensions in pixels
  uint8_t xAdvance;      ///< Distance to advance cursor (x axis)
  int8_t xOffset;        ///< X dist from cursor pos to UL corner
  int8_t yOffset;        ///< Y dist from cursor pos to UL corner
  uint8_t iso8859;	//
} glyph_t;

/*
/// Data stored for FONT AS A WHOLE
typedef struct {
  const char *name;
  const uint8_t *bitmap;  ///< Glyph bitmaps, concatenated, in row-major format
  const GFXglyph *glyph;     ///< Glyph array
  uint16_t first;   ///< ASCII extents (first char)
  uint16_t last;    ///< ASCII extents (last char)
  uint8_t yAdvance; ///< Newline distance (y axis)
} GFXfont;
*/

typedef struct Font {
  const glyph_t *glyph;     ///< Glyph array
  const uint8_t *RMbitmap;  ///< Glyph bitmaps, concatenated, in row-major format
  const uint8_t *BCMbitmap;  ///< Glyph bitmaps, concatenated, in byte-column major format
  uint8_t first;   ///< ASCII extents (first char)
  uint8_t last;    ///< ASCII extents (last char)
  uint8_t extra;   // number of extra glyphs
  uint8_t yAdvance; ///< Newline distance (y axis)
  int8_t minY;		// highest position over base line
  uint8_t maxY;		// lowest position under base line
  uint8_t blOff;	// offset of baseline to y-max pos. (i.e. height of 'A')
  uint8_t maxW;		// maximum character width
  const char *name;
#ifdef __cplusplus
	Font()
	: glyph(0)
	, RMbitmap(0)
	, BCMbitmap(0)
	, first(0)
	, last(0)
	, extra(0)
	, yAdvance(0)
	, minY(0),maxY(0)
	, blOff(0),maxW(0)
	, name(0)
	{ }

	unsigned getCharWidth(uint16_t ch) const;
	int8_t charMinY(uint16_t c) const;	// todo
	void getTextDim(const char *str, uint16_t &W, int8_t &ymin, int8_t &ymax) const;
	const glyph_t *getGlyph(uint16_t c) const;
#endif
} font_t;

typedef struct FontHdr
{
	char magic[4];
	char name[28];	// including terminating \0
	uint16_t first, last, yAdv, offGlyph;
	uint32_t size;	// bitmap size in bytes, all bitmaps have the same size
	uint32_t offRM,offBCM;
	uint16_t extra;	// number of extra glyphs
	uint8_t reserved[10];
} fonthdr_t;

//typedef struct Font Font;

typedef enum {
	font_default = -1,
	font_tiny = 0,
	font_small,
	font_medium,
	font_large,
	font_numfonts
} fontid_t;

#endif // _GFXFONT_H_
