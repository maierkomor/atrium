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

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ctype.h>
#include <ft2build.h>
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_TRUETYPE_DRIVER_H

#include "../drv/display/font.h"

#define ASCII_FIRST	0x20
#define ASCII_LAST	0x7e

static uint8_t ExtCharSet[] = {
	0xa7,		// paragraph
	0xa9,		// copyright
	0xb0,		// degree
	0xb1,		// plus/minus
	0xb2,		// square
	0xb3,		// raise-3
	0xb5,		// micro
	0xbc,		// quarter
	0xbd,		// half
	0xbe,		// three-quarter
	0xb7,		// times dot
	0xd7,		// times cross
	0xf7,		// division
};

//static uint8_t DefaultSizes[] = { 6,8,10,12,14,16,18,20,24,28,32,36,40 };
static uint8_t DefaultSizes[] = { 9,12,18 };
static uint8_t *Sizes = 0;
static unsigned NumSizes = 0;
static const char **Names = 0;
static unsigned NumNames = 0;


static FT_Library FTL;
static unsigned DPI = 100;
static uint8_t Debug = 0, Mode = 3, GenLib = 0, TomThumb = 0;
static FILE *Out = 0;


// get bit in row-major orientation
static uint8_t getBitRM(const uint8_t *in, uint8_t x, uint8_t y, uint8_t w)
{
	unsigned idx = x + y * w;
	return in[idx>>3] & (0x80 >> (idx & 7));
}


// set bit in column-major byte-page orientation
// for SSD1306
static void setBit(uint8_t *out, uint8_t x, uint8_t y, uint8_t h)
{
	unsigned off = x * h + y;
	out[off>>3] |= 1 << (off & 7);
}


static int isValidIdentifier(const char *s)
{
	for (;;) {
		char c = *s++;
		if ((c >= 'a') && (c <= 'z')) {
		} else if ((c >= 'A') && (c <= 'Z')) {
		} else if ((c >= '0') && (c <= '9')) {
		} else if (c == '_') {
		} else if (c == '0') {
			return 1;
		} else {
			return 0;
		}
	}
}


static int mkIdentifier(const char *in, char *out)
{
	const char *s = in;
	for (;;) {
		char c = *in++;
		if ((c >= 'a') && (c <= 'z')) {
			*out++ = c;
		} else if ((c >= 'A') && (c <= 'Z')) {
			*out++ = c;
		} else if ((c >= '0') && (c <= '9')) {
			*out++ = c;
		} else if (c == '_') {
			*out++ = c;
		} else if ((c == '-') || (c == '+') || (c == '*') || (c == '/') || (c == '.')) {
			*out++ = '_';
		} else if (c == 0) {
			*out = 0;
			return out-s;
		}
	}
}


void printCharRM(const uint8_t *data, size_t off, uint8_t w, uint8_t h, char c)
{
	fprintf(stderr,"\nglyph of '%c' %ux%u, offset 0x%lx - in RM:\n",c,w,h,off);
	data += off;
	char bmp[w+1];
	bmp[sizeof(bmp)-1] = 0;
	for (int y = 0; y < h; ++y) {
		memset(bmp,' ',w);
		for (int x = 0; x < w; ++x) {
			if (getBitRM(data,x,y,w))
				bmp[x] = 'X';
		}
		fprintf(stderr,"%s\n",bmp);
	}
}


void printCharBCM(const uint8_t *data, unsigned off, uint8_t w, uint8_t h, char c)
{
	data += off;
	char bmp[w*h+h+1];
	memset(bmp,' ',sizeof(bmp));
	for (int l = 0; l < h; ++l)
		bmp[(w+1)*l-1] = '\n';
	bmp[sizeof(bmp)-1] = 0;
	for (int x = 0; x < w; ++x) {
		for (int y = 0; y < h; ++y) {
			unsigned off = x * h + y;
			if (data[off>>3] & (1 << (off & 7)))
				bmp[y*(w+1)+x] = 'X';
		}
	}
	fprintf(stderr,"\nglyph of '%c' %ux%u - in BCM:\n%s\n",c,w,h,bmp);
}


void transposeChar(const uint8_t *in, uint8_t w, uint8_t h, int p, uint8_t *out)
{
	for (uint8_t x = 0; x < w; ++x) {
		for (uint8_t y = 0; y < h; ++y) {
			uint8_t xbyte = x >> 3;
			uint8_t xbit = 0x80 >> (x & 7);
			if (in[y * p + xbyte] & xbit) {
				setBit(out,x,y,h);
			}
		}
	}
}

static unsigned output_char_bitmap_rm(FT_Face face, uint8_t ch)
{
	int err = FT_Load_Char(face,ch,FT_LOAD_TARGET_MONO);
	if (err) {
		fprintf(stderr,"loading char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
	if (err) {
		fprintf(stderr,"render char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	FT_Glyph glyph;
	err = FT_Get_Glyph(face->glyph, &glyph);
	if (err) {
		fprintf(stderr,"glyph of char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	FT_BitmapGlyphRec *grec = (FT_BitmapGlyphRec *)glyph;
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	fprintf(Out,"\n\t// '%c'\n",ch);
	unsigned off = 0, bits = 0;
	uint8_t obyte = 0, obit = 0x80, line = 0;
	char sep = '\t';
	for (unsigned y = 0; y < bitmap->rows; ++y) {
		for (unsigned x = 0; x < bitmap->width; ++x) {
			uint8_t xbyte = x >> 3;
			uint8_t xbit = 0x80 >> (x & 7);
			char c = ' ';
			if (bitmap->buffer[y * bitmap->pitch + xbyte] & xbit) {
				obyte |= obit;
				c = 'X';
			}
			obit >>= 1;
			if (obit == 0) {
				obit = 0x80;
				fprintf(Out,"%c0x%02x,",sep,obyte);
				obyte = 0;
				++off;
				++line;
				if (((off & 15) == 0) || (8 == line)) {
					sep = '\t';
					line = 0;
					fprintf(Out,"\n");
				} else {
					sep = ' ';
				}
			}
			if (Debug)
				printf("%c",c);
		}
		if (Debug)
			printf("\n");
	}
	if (obit != 0x80) {
		fprintf(Out,"%c0x%02x,\n",sep,obyte);
	} else {
		fprintf(Out,"\n");
	}
	unsigned size = bitmap->rows*bitmap->width;
	return (size >> 3) + ((size&7) != 0);
}


static unsigned output_char_bitmap_bcm(FT_Face face, uint8_t ch)
{
	int err = FT_Load_Char(face,ch,FT_LOAD_TARGET_MONO);
	if (err) {
		fprintf(stderr,"loading char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
	if (err) {
		fprintf(stderr,"render char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	FT_Glyph glyph;
	err = FT_Get_Glyph(face->glyph, &glyph);
	if (err) {
		fprintf(stderr,"glyph of char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	FT_BitmapGlyphRec *grec = (FT_BitmapGlyphRec *)glyph;
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	uint8_t *rmbuf = bitmap->buffer;
	unsigned size = bitmap->rows*bitmap->width;
	unsigned bytes = (size >> 3) + ((size & 7) != 0);
	uint8_t bcmbuf[bytes];
	bzero(bcmbuf,sizeof(bcmbuf));
	transposeChar(rmbuf,bitmap->width,bitmap->rows,bitmap->pitch,bcmbuf);
//	printCharRM(rmbuf,bitmap->pitch,bitmap->rows,ch);
	if (Debug)
		printCharBCM(bcmbuf,0,bitmap->width,bitmap->rows,ch);
	fprintf(Out,"\n\t// '%c'\n",ch);
	char sep = '\t';
	unsigned off = 0;
	for (uint8_t *at = bcmbuf, *end = bcmbuf+sizeof(bcmbuf); at != end; ++at) {
		fprintf(Out,"%c0x%02x,",sep,*at);
		++off;
		if (off & 7) {
			sep = ' ';
		} else {
			fprintf(Out,"\n");
			sep = '\t';
		}
	}
	return (size >> 3) + ((size&7) != 0);
}


static unsigned calc_char_bitmapsize(FT_Face face, uint8_t ch)
{
	int err = FT_Load_Char(face,ch,FT_LOAD_TARGET_MONO);
	if (err) {
		fprintf(stderr,"loading char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
	if (err) {
		fprintf(stderr,"render char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	FT_Glyph glyph;
	err = FT_Get_Glyph(face->glyph, &glyph);
	if (err) {
		fprintf(stderr,"glyph of char 0x%x: %s\n",ch,FT_Error_String(err));
		return 0;
	}
	FT_BitmapGlyphRec *grec = (FT_BitmapGlyphRec *)glyph;
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	FT_Done_Glyph(glyph);
	unsigned size = bitmap->rows*bitmap->width;
	return (size >> 3) + ((size&7) != 0);
}


static unsigned output_char_glyph(FT_Face face, uint8_t ch, unsigned offset)
{
	int err = FT_Load_Char(face,ch,FT_LOAD_TARGET_MONO);
	if (err) {
		fprintf(stderr,"loading char 0x%x: %s\n",ch,FT_Error_String(err));
		return offset;
	}
	err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
	if (err) {
		fprintf(stderr,"render char 0x%x: %s\n",ch,FT_Error_String(err));
		return offset;
	}
	FT_Glyph glyph;
	err = FT_Get_Glyph(face->glyph, &glyph);
	if (err) {
		fprintf(stderr,"glyph of char 0x%x: %s\n",ch,FT_Error_String(err));
		return offset;
	}
	FT_BitmapGlyphRec *grec = (FT_BitmapGlyphRec *)glyph;
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	fprintf(Out,"\t{\n");
	fprintf(Out,"\t\t// glyph '%c'\n",ch);
	fprintf(Out,"\t\t0x%x,\t// offset\n",offset);
	fprintf(Out,"\t\t%u,\t// width\n",bitmap->width);
	fprintf(Out,"\t\t%u,\t// height\n",bitmap->rows);
	fprintf(Out,"\t\t%lu,\t// xAdvance\n",face->glyph->advance.x>>6);
	fprintf(Out,"\t\t%d,\t// xOffset\n",grec->left);
	fprintf(Out,"\t\t%d,\t// yOffset\n",1-grec->top);
	fprintf(Out,"\t\t0x%x,\t// iso8859 char\n",ch);
	fprintf(Out,"\t},\n");

	FT_Done_Glyph(glyph);
	unsigned size = bitmap->rows*bitmap->width;
	return offset + (size >> 3) + ((size&7) != 0);
}


static void process_font_size(FT_Face face, char *name, uint8_t size)
{
	size_t nl = strlen(name);
	char id[nl+1];
	mkIdentifier(name,id);
	if (0 == GenLib) {
		char filename[nl + 16];
		sprintf(filename,"%s-%u.c",name,size);
		Out = fopen(filename,"w");
		fprintf(Out,"#include \"font.h\"\n\n");
	}
	fprintf(Out,"// font %s, size %u\n\n",name,size);
	unsigned bs = 0;
	FT_Set_Char_Size(face, size << 6, 0, DPI, 0);
	unsigned numch = 0;
	for (uint8_t ch = ASCII_FIRST; ch <= ASCII_LAST; ++ch) {
		bs += calc_char_bitmapsize(face,ch);
		++numch;
	}
	for (uint8_t xch = 0; xch < sizeof(ExtCharSet)/sizeof(ExtCharSet[0]); ++xch) {
		bs += calc_char_bitmapsize(face,ExtCharSet[xch]);
		++numch;
	}
	unsigned rmoff = numch*sizeof(glyph_t)+sizeof(fonthdr_t);
	unsigned bcmoff = rmoff+bs;
	if (bcmoff & 0xf) {
		bcmoff &= ~0xf;
		bcmoff += 0x10;
	}
	if (0 == GenLib) {
		fprintf(Out,
			"const fonthdr_t FontHdr = {\n"
			"	{ 'A','f','n','t' },\n"
			"	\"%s-%u\",\n"
			"	%u,\t// first\n"
			"	%u,\t// last\n"
			"	%lu,\t// yAdv\n"
			"	%lu,\t// offGlyph\n"
			"	%u,\t// bitmap size\n"
			"#ifdef AF2\n"
			"	0,\t// no RM\n"
			"#else\n"
			"	%u,\t// offset RM\n"
			"#endif // AF2\n"
			"#ifdef AF1\n"
			"	0,\t// no BCM\n"
			"#elif defined AF2\n"
			"	%u,\t// offset BCM (w/o RM)\n"
			"#else\n"
			"	%u,\t// offset BCM\n"
			"#endif // AF1\n"
			"	%lu,\t// number of extra glyphs\n"
			"};\n"
			, name,size
			, ASCII_FIRST, ASCII_LAST
			, face->size->metrics.height >> 6	// yAdvance
			, sizeof(fonthdr_t)			// offGlyph
			, bs					// bitmap size
			, rmoff
			, rmoff
			, bcmoff
			, sizeof(ExtCharSet)/sizeof(ExtCharSet[0])
			);
		fprintf(Out,"\n\nstatic const glyph_t Glyphs[] = {\n");
	} else {
		fprintf(Out,"\n\nstatic const glyph_t %s_%u_Glyphs[] = {\n",id,size);
	}
	unsigned offset = 0;
	for (uint8_t ch = ASCII_FIRST; ch <= ASCII_LAST; ++ch) {
		offset = output_char_glyph(face,ch,offset);
	}
	for (uint8_t xch = 0; xch < sizeof(ExtCharSet)/sizeof(ExtCharSet[0]); ++xch) {
		offset = output_char_glyph(face,ExtCharSet[xch],offset);
	}
	if (0 == GenLib) {
		fprintf(Out,	"};\n\n\n"
				"#ifndef AF2\n"
				"static const uint8_t RmBitmaps[] = {");
	} else {
		fprintf(Out,	"};\n\n\n"
				"static const uint8_t %s_%u_RM[] = {",id,size);
	}
	for (uint8_t ch = ASCII_FIRST; ch <= ASCII_LAST; ++ch) {
		output_char_bitmap_rm(face,ch);
	}
	for (uint8_t xch = 0; xch < sizeof(ExtCharSet)/sizeof(ExtCharSet[0]); ++xch) {
		output_char_bitmap_rm(face,ExtCharSet[xch]);
	}
	if (0 == GenLib) {
		fprintf(Out,	"\n};\n#endif // AF2\n\n\n"
				"#ifndef AF1\n"
				"static const uint8_t BcmBitmaps[] = {");
	} else {
		fprintf(Out,	"\n};\n\n\n"
				"static const uint8_t %s_%u_BCM[] = {",id,size);
	}
	for (uint8_t ch = ASCII_FIRST; ch <= ASCII_LAST; ++ch) {
		output_char_bitmap_bcm(face,ch);
	}
	for (uint8_t xch = 0; xch < sizeof(ExtCharSet)/sizeof(ExtCharSet[0]); ++xch) {
		output_char_bitmap_bcm(face,ExtCharSet[xch]);
	}
	if (0 == GenLib) {
		fprintf(Out,	"\n};\n#endif // AF1\n\n\n");
	} else {
		fprintf(Out,	"\n};\n\n"
				/*
				"font_t Font_%s_%u = {\n"
				"\tGlyphs_%s,\n"
				"\tRmBitmap_%s,\n"
				"\tBcmBitmap_%s,\n"
				"\t%u, %u,// first, last\n"
				"\t%lu, %lu,// extra, yAdvance\n"
				"\t%d, %d,// minY, maxY\n"
				"\t%u, %u,// blOff, maxW\n"
				"};\n\n"
				, id, size, id, id, id
				, ASCII_FIRST, ASCII_LAST
				, sizeof(ExtCharSet)/sizeof(ExtCharSet[0])
				, face->size->metrics.height >> 6	// yAdvance
				, 0, 0, 0, 0
				*/
		       );
	}
	if (0 == GenLib) {
		fclose(Out);
		Out = 0;
	}
}


static void process_font(const char *font)
{
	size_t l = strlen(font);
	const char *sl = strrchr(font,'/');
	char *name;
	if (sl) {
		name = strdup(sl+1);
	} else {
		name = strdup(font);
	}
	char *dot = strrchr(name,'.');
	if (dot)
		*dot = 0;
	FT_Face face;
	int err = FT_New_Face(FTL,font,0,&face);
	if (err) {
		fprintf(stderr,"Failed to load font file %s: %s\n",font,FT_Error_String(err));
		return;
	}
	for (unsigned s = 0; s < NumSizes; ++s) {
		process_font_size(face,name,Sizes[s]); }
	free(name);
}


static void glyph_metrics(FT_Face face, unsigned ch, int8_t *minY, int8_t *maxY, uint8_t *maxW, uint8_t *blOff)
{
	int err = FT_Load_Char(face,ch,FT_LOAD_TARGET_MONO);
	if (err) {
		fprintf(stderr,"loading char 0x%x: %s\n",ch,FT_Error_String(err));
		return;
	}
	err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
	if (err) {
		fprintf(stderr,"render char 0x%x: %s\n",ch,FT_Error_String(err));
		return;
	}
	FT_Glyph glyph;
	err = FT_Get_Glyph(face->glyph, &glyph);
	if (err) {
		fprintf(stderr,"glyph of char 0x%x: %s\n",ch,FT_Error_String(err));
		return;
	}
	FT_BitmapGlyphRec *grec = (FT_BitmapGlyphRec *)glyph;
	FT_Bitmap *bitmap = &face->glyph->bitmap;
	uint8_t w = grec->left+bitmap->width;
	uint8_t maxy = grec->top+bitmap->rows;
	if (grec->top < *minY)
		*minY = grec->top;
	if (maxy > *maxY)
		*maxY = maxy;
	if (w > *maxW)
		*maxW = w;
	if ('0' == ch)
		*blOff = maxy-grec->top;
	FT_Done_Glyph(glyph);
}


static char *gen_font_info(const char *font)
{
	size_t l = strlen(font);
	const char *sl = strrchr(font,'/');
	char *name;
	if (sl) {
		name = strdup(sl+1);
	} else {
		name = strdup(font);
	}
	char *dot = strrchr(name,'.');
	if (dot)
		*dot = 0;
	size_t nl = strlen(name);
	char id[nl+1];
	mkIdentifier(name,id);
	FT_Face face;
	int err = FT_New_Face(FTL,font,0,&face);
	if (err) {
		fprintf(stderr,"Failed to load font file %s: %s\n",font,FT_Error_String(err));
		return 0;
	}
	for (unsigned s = 0; s < NumSizes; ++s) {
		unsigned size = Sizes[s];
		int8_t minY = INT8_MAX, maxY = INT8_MIN;
		uint8_t maxW = 0, blOff = 0;
		FT_Set_Char_Size(face, size << 6, 0, DPI, 0);
		for (uint8_t ch = ASCII_FIRST; ch <= ASCII_LAST; ++ch) {
			glyph_metrics(face,ch,&minY,&maxY,&maxW,&blOff);
		}
		for (uint8_t xch = 0; xch < sizeof(ExtCharSet)/sizeof(ExtCharSet[0]); ++xch) {
			glyph_metrics(face,ExtCharSet[xch],&minY,&maxY,&maxW,&blOff);
		}
		fprintf(Out,
			"const font_t %s_%u = {\n"
			"\t%s_%u_Glyphs,\n"
			"\t%s_%u_RM,\n"
			"\t%s_%u_BCM,\n"
			"\t%u, %u,	// ASCII range\n"
			"\t%lu,	// extra chars\n"
			"\t%lu,	// yAdvance\n"
			"\t%d,%d,	// minY, maxY\n"
			"\t%u,%u,	// blOff, maxW\n"
			"\t\"%s-%u\"\n"
			"};\n\n"
			, id, size
			, id, size
			, id, size
			, id, size
			, ASCII_FIRST, ASCII_LAST
			, sizeof(ExtCharSet)/sizeof(ExtCharSet[0])
			, face->size->metrics.height >> 6	// yAdvance
			, minY, maxY, blOff, maxW
			, name, size
		       );
	}
	return strdup(id);
}


unsigned parseFont(const uint8_t *data, size_t s, font_t *f)
{
	bzero(f,sizeof(font_t));
	struct FontHdr *hdr = (struct FontHdr *)data;
	if (memcmp(hdr->magic,"Afnt",4)) {
		return 0;
	}
	if (Debug) {
		fprintf(stderr,"glyph at 0x%x, RM at 0x%x, BCM at 0x%x, size %u\n",
				hdr->offGlyph, hdr->offRM, hdr->offBCM, hdr->size);
	}
	if (hdr->offRM) {
		f->RMbitmap = data + hdr->offRM;
	} else {
		f->RMbitmap = 0;
	}
	if (hdr->offBCM) {
		f->BCMbitmap = data + hdr->offBCM;
	} else {
		f->BCMbitmap = 0;
	}
	f->glyph = (const glyph_t *)(data + hdr->offGlyph);
	f->first = hdr->first;
	f->last = hdr->last;
	f->extra = hdr->extra;
	f->yAdvance = hdr->yAdv;
	f->name = strndup(hdr->name,sizeof(hdr->name));
	((char*)f->name)[sizeof(hdr->name)] = 0;
	if (Debug) {
		unsigned n = f->last-f->first+1;
		for (unsigned x = 0; x <= n; ++x) {
			const glyph_t *g = f->glyph+x;
			unsigned s = (g->width*g->height)>>3;
			char ch = x + f->first;
//			print_hex(f->RMbitmap+f->glyph[x].bitmapOffset,s,"'%c' %ux%u",x+f->first,f->glyph[x].width,f->glyph[x].height);
			if (f->RMbitmap)
				printCharRM(f->RMbitmap,g->bitmapOffset,g->width,g->height,ch);
			if (f->BCMbitmap)
				printCharBCM(f->BCMbitmap,g->bitmapOffset,g->width,g->height,ch);
		}
	}
	return hdr->size;
	/*
	size_t bs;
	if (hdr->offRM > hdr->offBCM)
		bs = s - hdr->offRM;
	else
		bs = s - hdr->offBCM;
	return bs;
	*/
}


int loadFont(const char *fn, font_t *f)
{
	int fd = open(fn,O_RDONLY);
	if (fd == -1) {
		fprintf(stderr,"unable to open %s",fn);
		return -1;
	}
	struct stat st;
	int r = -1;
	uint8_t *data;
	if (-1 == fstat(fd,&st)) {
		fprintf(stderr,"failed to stat %s\n",fn);
	} else if (data = (uint8_t *) malloc(st.st_size)) {
		int n = read(fd,data,st.st_size);
		if (n == st.st_size) {
			r = parseFont(data,st.st_size,f);
		} else {
			fprintf(stderr,"failed to read %s\n",fn);
		}
	} else {
		fprintf(stderr,"unable alloc for %s\n",fn);
	}
	close(fd);
	return r;
}


void printFontFile(const char *ff)
{
	font_t font;
	int s = loadFont(ff,&font);
	if (s > 0) {
		glyph_t *g = (glyph_t*)&font.glyph[0];
		for (uint8_t ch = 0; ch <= font.last-font.first; ++ch) {
			unsigned off = g->bitmapOffset;
			if (font.RMbitmap)
				printCharRM(font.RMbitmap,off,g->width,g->height,ch+font.first);
			if (font.BCMbitmap)
				printCharBCM(font.BCMbitmap,off,g->width,g->height,ch+font.first);
			++g;
		}
		for (uint8_t xch = 0; xch < font.extra; ++xch) {
			unsigned off = g->bitmapOffset;
			if (font.RMbitmap)
				printCharRM(font.RMbitmap,off,g->width,g->height,xch);
			if (font.BCMbitmap)
				printCharBCM(font.BCMbitmap,off,g->width,g->height,xch);
			++g;
		}
	}
}


static void addSize(const char *str)
{
	char *e;
	long l = strtol(str,&e,0);
	while ((*e == 0) || (*e == ',')) {
		if ((l <= 4) || (l > UINT8_MAX)) {
			fprintf(stderr,"invalid size %ld\n",l);
			return;
		}
		++NumSizes;
		Sizes = realloc(Sizes,sizeof(*Sizes)*NumSizes);
		Sizes[NumSizes-1] = l;
		if (*e == 0)
			return;
		l = strtol(e+1,&e,0);
	}
}


static void addName(const char *name)
{
	if (name) {
		++NumNames;
		Names = realloc(Names,sizeof(*Names)*NumNames);
		Names[NumNames-1] = name;
	}
}


void usage()
{
	fprintf(stderr,
		"font-tool [<options>] [<fontfile>]\n"
		"valid options are:\n"
		"-1 : to generate only row-major data\n"
		"-2 : to generate only byte-column-major data\n"
		"-l : to generate a library source file\n"
		"-t : to include TomThumb font in the library\n"
	       );
	exit(EXIT_SUCCESS);
}


int main(int argc, char *argv[])
{
	int err;
	err = FT_Init_FreeType(&FTL);
	if (err) {
		fprintf(stderr,"freetype init failed: %d\n",err);
		return EXIT_FAILURE;
	}
	// no subpixel rendering
	FT_UInt vers = TT_INTERPRETER_VERSION_35;
	FT_Property_Set(FTL,"truetype","interpret-version",&vers);
	do {
		err = getopt(argc,argv,"12hlp:s:tv");
		switch (err) {
		case '1':
			Mode = 1;
			break;
		case '2':
			Mode = 2;
			break;
		case 'h':
			usage();
			break;
		case 'l':
			GenLib = 1;
			break;
		case 't':
			TomThumb = 1;
			break;
		case 's':
			addSize(optarg);
			break;
		case 'v':
			Debug = 1;
			break;
		case 'p':
			printFontFile(optarg);
			break;
		case '?':
			break;
		case -1:
			break;
		default:
			break;
		}
	} while (err != -1);
	if (0 == Sizes) {
		NumSizes = sizeof(DefaultSizes)/sizeof(DefaultSizes[0]);
		Sizes = DefaultSizes;
	}
	if (GenLib) {
		Out = stdout;
		fprintf(Out,
			"#include <stdint.h>\n"
			"#include <stddef.h>\n"
			"#include \"font.h\"\n\n"
		);
		if (TomThumb) {
			fprintf(Out, "#include \"TomThumb.h\"\n");

		}
	}
	for (int c = optind; c < argc; ++c) {
		process_font(argv[c]);
	}
	if (GenLib) {
		if (TomThumb) {
			fprintf(Out,"const font_t TomThumb ={\nTOM_THUMB_FONT_DEF\n};\n\n");
		}
		for (int c = optind; c < argc; ++c) {
			addName(gen_font_info(argv[c]));
		}
		fprintf(Out,"const font_t *Fonts[] = {\n");
		for (int n = 0; n < NumNames; ++n) {
			for (unsigned s = 0; s < NumSizes; ++s) {
				unsigned size = Sizes[s];
				fprintf(Out,"\t&%s_%u,\n",Names[n],size);
			}
		}
		fprintf(Out,"};\n\nsize_t NumFonts = sizeof(Fonts)/sizeof(Fonts[0]);\n\n");
	}
	exit(EXIT_SUCCESS);
}
