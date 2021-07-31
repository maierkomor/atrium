#ifndef FONTS_SSD1306_H
#define FONTS_SSD1306_H

#include "font.h"

typedef enum {
	font_nativedbl = -2,
	font_native = -1,
	font_mono9 = 0,
	font_mono12,
	font_mono18,
	font_mono24,
	font_tomthumb,
	font_sans9,
	font_sans12,
	font_sanslight10,
	font_sanslight12,
	font_sanslight14,
	font_sanslight16,
	font_org01,
	font_numfonts
} fontid_t;

extern const Font Fonts[];
#endif
