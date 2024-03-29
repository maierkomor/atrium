// Font structures for newer Adafruit_GFX (1.1 and later).
// Example fonts are included in 'Fonts' directory.
// To use a font in your Arduino sketch, #include the corresponding .h
// file and pass address of Font struct to setFont().  Pass NULL to
// revert to 'classic' fixed-space bitmap font.

#ifndef _FONT_H_
#define _FONT_H_

#include <stdint.h>

/// Font data stored PER GLYPH
typedef struct {
  uint16_t bitmapOffset; ///< Pointer into Font->bitmap
  uint8_t width;         ///< Bitmap dimensions in pixels
  uint8_t height;        ///< Bitmap dimensions in pixels
  uint8_t xAdvance;      ///< Distance to advance cursor (x axis)
  int8_t xOffset;        ///< X dist from cursor pos to UL corner
  int8_t yOffset;        ///< Y dist from cursor pos to UL corner
} GFXglyph;

/// Data stored for FONT AS A WHOLE
typedef struct {
  const char *name;
  const uint8_t *bitmap;  ///< Glyph bitmaps, concatenated, in row-major format
  const GFXglyph *glyph;     ///< Glyph array
  uint16_t first;   ///< ASCII extents (first char)
  uint16_t last;    ///< ASCII extents (last char)
  uint8_t yAdvance; ///< Newline distance (y axis)
} GFXfont;

typedef struct {
  const uint8_t *RMbitmap;  ///< Glyph bitmaps, concatenated, in row-major format
  const uint8_t *BCMbitmap;  ///< Glyph bitmaps, concatenated, in byte-column major format
  const GFXglyph *glyph;     ///< Glyph array
  uint8_t first;   ///< ASCII extents (first char)
  uint8_t last;    ///< ASCII extents (last char)
  uint8_t yAdvance; ///< Newline distance (y axis)
  const char name[17];
} Font;

typedef enum {
//	font_nativedbl = -2,
//	font_native = -1,
	font_mono9 = 0,
	font_mono12,
	font_mono18,
//	font_mono24,
	font_tomthumb,
//	font_sans9,
//	font_sans12,
	font_sanslight10,
	font_sanslight12,
	font_sanslight14,
	font_sanslight16,
	font_org01,
	font_numfonts
} fontid_t;

#endif // _GFXFONT_H_
