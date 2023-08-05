#ifndef FONTS_H
#define FONTS_H

#include "font.h"

// row-major definitions (standard layout)
extern const Font FontsRM[];
extern uint8_t NumFontsRM;
extern const uint8_t Font6x8[];
extern const uint16_t SizeofFont6x8;

// byte-column-major definitions for SSD130x family
extern const Font FontsBCM[];
extern uint8_t NumFontsBCM;

#endif
