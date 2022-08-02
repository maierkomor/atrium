/****************************************************************************
 * 
 * Code generated by Wire-Format-Compiler (WFC)
 * WFC Version: R2104.48 (hg:436/e1544d21e885)
 * WFC is Copyright 2015-2021, Thomas Maier-Komor
 * 
 * Source Information:
 * ===================
 * Filename : hwcfg.wfc
 * Copyright: 2018-2021
 * Author   : Thomas Maier-Komor
 * 
 * Code generated on 2022-08-01, 00:43:58 (CET).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 ****************************************************************************/


/*
 * options from commandline:
 * wfclib          : "extern"
 * 
 * options from esp32:
 * endian          : "little"
 * Optimize        : "speed"
 * 
 * options from esp:
 * bytestype       : "estring"
 * intsize         : 32
 * SortMembers     : "size"
 * stringtype      : "estring"
 * toASCII         : "toASCII"
 * toSink          : ""
 * toString        : ""
 * toWire          : ""
 * varintbits      : 32
 * 
 * options from common:
 * 
 * options from defaults:
 * AddPrefix       : "add_"
 * ascii_bool      : "ascii_bool"
 * ascii_bytes     : "ascii_bytes"
 * ascii_indent    : "ascii_indent"
 * ascii_string    : "ascii_string"
 * author          : ""
 * BaseClass       : ""
 * calcSize        : "calcSize"
 * ClearName       : "clear"
 * ClearPrefix     : "clear_"
 * copyright       : ""
 * email           : ""
 * ErrorHandling   : "cancel"
 * fromMemory      : "fromMemory"
 * GetPrefix       : ""
 * HasPrefix       : "has_"
 * inline          : ""
 * json_indent     : "json_indent"
 * lang            : "c++"
 * MutablePrefix   : "mutable_"
 * namespace       : ""
 * SetByName       : "setByName"
 * SetPrefix       : "set_"
 * toJSON          : "toJSON"
 * toMemory        : "toMemory"
 * UnknownField    : "skip"
 * wireput         : ""
 * wiresize        : ""
 * 
 * enabled flags from esp32:
 * 	withUnequal
 * enabled flags from esp:
 * 	enumnames, withEqual
 * enabled flags from common:
 * 	id0
 * disabled flags from defaults:
 * 	debug, SubClasses
 * enabled flags from defaults:
 * 	asserts, comments, genlib, gnux
 */

#include "wfccore.h"

#include <map>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

/* included from: (WFC_ROOT)/share/write_varint.cct
 * function:      write_varint
 * variant:       write_varint_generic
 */
int write_varint(uint8_t *wire, ssize_t wl, varint_t v)
{
	uint8_t *w = wire;
	do {
		if (--wl < 0)
			return -1;
		uint8_t u8 = v & 0x7f;
		v >>= 7;
		if (v)
			u8 |= 0x80;
		*wire = u8;
		++wire;
	} while (v);
	return wire-w;
}


/* included from: (WFC_ROOT)/share/wiresize.cc
 * function:      wiresize
 * variant:       wiresize_generic
 */
unsigned wiresize(varint_t u)
{
	unsigned x = 0;
	do ++x;
	while (u>>=7U);
	return x;
}


/* included from: (WFC_ROOT)/share/wiresize_s.cc
 * function:      wiresize_s
 * variant:       wiresize_s_32
 * varintbits: 32
 */
unsigned wiresize_s(int32_t s)
{
	uint32_t u = (s << 1) ^ (s >> 31);
	return wiresize(u);
}


/* included from: (WFC_ROOT)/share/wiresize_x.cc
 * function:      wiresize_x
 * variant:       wiresize_x_generic
 */
unsigned wiresize_x(varsint_t v)
{
	if (v < 0)
		return 10;
	unsigned x = 0;
	do ++x;
	while (v>>=7U);
	return x;
}


/* included from: (WFC_ROOT)/share/early_decode.cct
 * function:      decode_early
 * variant:       decode_early
 */
int decode_early(const uint8_t *s, const uint8_t *e, union decode_union *ud, varint_t *fid)
{
	int fn = read_varint(s,e-s,fid);
	if (fn <= 0)
		return -2;
	const uint8_t *a = s + fn;
	switch ((*fid)&7) {
	case 0x0: // varint
	case 0x2: // varint of len pfx array
		fn = read_varint(a,e-a,&ud->vi);
		if (fn <= 0)
			return -3;
		a += fn;
		break;
	case 0x1: // 64-bit
		if (a+8 > e)
			return -4;
		ud->u64 = read_u64(a);
		a += 8;
		break;
	case 0x3: // 8-bit
		if (a+1 > e)
			return -5;
		ud->u64 = 0;
		ud->u8 = *a++;
		break;
	case 0x4: // 16-bit
		if (a+2 > e)
			return -6;
		ud->u32 = 0;
		ud->u16 = read_u16(a);
		a += 2;
		break;
	case 0x5: // 32-bit
		if (a+4 > e)
			return -7;
		ud->u32 = 0;
		ud->u32 = read_u32(a);
		a += 4;
		break;
	default:
		return -8;
	}
	return a-s;
}


/* included from: (WFC_ROOT)/share/read_varint.cct
 * function:      read_varint
 * variant:       read_varint_default
 */
unsigned read_varint(const uint8_t *wire, ssize_t wl, varint_t *r)
{
	uint8_t u8;
	int n = 0;
	varint_t v = 0;
	do {
		if (--wl < 0)
			return -9;
		u8 = *wire++;
		v |= (varint_t)(u8&~0x80) << (n*7);
		++n;
	} while ((u8 & 0x80) && (n < (32/7+1)));
	*r = v;
	if ((32 != 64) && (u8 & 0x80)) {
		while (*wire++ & 0x80)
			++n;
		++n;
	}
	return n;
}


/* included from: (WFC_ROOT)/share/skip_content.cct
 * function:      skip_content
 * variant:       skip_content
 */
ssize_t skip_content(const uint8_t *wire, ssize_t wl, unsigned type)
{
	ssize_t n;
	switch (type) {
	case 0:
		n = 0;
		while (wire[n++]&0x80) {
			if (wl == n)
				return -10;
		}
		break;
	case 1:
		n = 8;
		break;
	case 2: {
			varint_t v = 0;
			unsigned l = read_varint(wire,wl,&v);
			if (0 == l)
				return -11;
			n = v + l;
			break;
		}
	case 3:
		n = 1;
		break;
	case 4:
		n = 2;
		break;
	case 5:
		n = 4;
		break;
	default:
		n = 0;
		return -12;
	}
	if (n > wl)
		return -13;
	return n;
}


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_indent
 * variant:       ascii_indent
 */
void ascii_indent(stream &out, ssize_t n, const char *fname )
{
	out.put('\n');
	while (n > 0) {
		out.put('\t');
		--n;
	}
	if (fname) {
		out << fname;
		out.write(" = ",3);
	}
}


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_bool
 * variant:       ascii_bool
 */
void ascii_bool(stream &out, ssize_t n, const char *fname, bool v)
{
	out.put('\n');
	while (n > 0) {
		out.put('\t');
		--n;
	}
	if (fname) {
		out << fname;
		out.write(" = ",3);
	}
	out << (v ? "true;" : "false;");
}


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_bytes
 * variant:       ascii_bytes
 */
void ascii_bytes(stream &out, const uint8_t *str, size_t len, size_t indent, const char *fname )
{
	if (fname) {
		out.put('\n');
		size_t n = indent;
		while (n > 0) {
			out.put('\t');
			--n;
		}
		out << fname;
		out.write(" = ",3);
	}
	static char hex_table[] = "0123456789abcdef";
	unsigned cil = 0;		// charakters in one line
	bool mline = (len > 16);	// more than one line?
	out << '{';
	if (mline) {
		ascii_indent(out,indent);
	} else {
		out << ' ';
	}
	while (len) {
		uint8_t c = *str++;
		out << hex_table[(c >> 4)&0xf] << hex_table[c&0xf];
		--len;
		++cil;
		if (len) {
			out << ',';
			if ((cil & 0x3) == 0)
				out << ' ';
			if (mline && (cil == 8)) {
				out << ' ';
			} else if (cil == 16) {
				out << '\n';
				ascii_indent(out,indent);
				cil = 0;
			}
		}
	}
	if (mline) {
		out << '\n';
		ascii_indent(out,indent-1);
	} else {
		out << ' ';
	}
	out << '}';
}


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_string
 * variant:       ascii_string
 */
void ascii_string(stream &out, size_t indent, const char *str, size_t len, const char *fname )
{
	if (fname) {
		out.put('\n');
		size_t n = indent;
		while (n > 0) {
			out.put('\t');
			--n;
		}
		out << fname;
		out.write(" = ",3);
	}
	unsigned cil = 0;
	out << '"';
	while (len) {
		char c = *str++;
		--len;
		if (cil == 64) {
			out << "\"\n";
			ascii_indent(out,indent);
			out << '"';
			cil = 0;
		}
		++cil;
		if ((c >= 0x23) && (c <= 0x7e)) {
			if (c == 0x5c) {
				// '\' itself must be escaped
				out << '\\';
			}
			// 0-9, a-z A-Z
			// ']'..'~', some operators, etc
			out << c;
			continue;
		}
		switch (c) {
		case '\0': out << "\\0"; continue;
		case '\t': out << "\\t"; continue;
		case '\r': out << "\\r"; continue;
		case '\b': out << "\\b"; continue;
		case '\a': out << "\\a"; continue;
		case '\e': out << "\\e"; continue;
		case '\f': out << "\\f"; continue;
		case '\v': out << "\\v"; continue;
		case '\\': out << "\\\\"; continue;
		case '"' : out << "\\\""; continue;
		case ' ' : out << ' '; continue;
		case '!' : out << '!'; continue;
		case '\n':
			out << "\\n\"\n";
			ascii_indent(out,indent);
			out << '"';
			continue;
		default:
			out << "\\0" << (unsigned)((c>>6)&0x3) << (unsigned)((c>>3)&0x7) << (unsigned)(c&0x7);
		}
	}
	out << "\";";
}


/* included from: (WFC_ROOT)/share/json_string.cct
 * function:      json_cstr
 * variant:       json_cstr_buffered
 * Optimize: speed
 */
void json_cstr(stream &json, const char *cstr)
{
	char buf[32], *at = buf;
	*at++ = '"';
	if (cstr) {
		while (char c = *cstr++) {
			if ((c >= 0x20) && (c <= 0x7e)) {
				if ((c == '\\') || (c == '"'))
					*at++ = '\\';
				*at++ = c;
			} else {
				*at++ = '\\';
				switch (c) {
				case '\n':
					*at++ = 'n';
					break;
				case '\r':
					*at++ = 'r';
					break;
				case '\b':
					*at++ = 'b';
					break;
				case '\f':
					*at++ = 'f';
					break;
				case '\t':
					*at++ = 't';
					break;
				default:
					at += sprintf(at,"u%04x",(unsigned)(uint8_t)c);
				}
			}
			// reserve for a complete iteration max - i.e. '\\uxxxx'\0
			if (buf+sizeof(buf) <= at+8) {
				json.write(buf,at-buf);
				at = buf;
			}
		}
	}
	*at++ = '"';
	json.write(buf,at-buf);
}


/* included from: (WFC_ROOT)/share/json_string.cct
 * function:      to_dblstr
 * variant:       to_dblstr_str
 */
void to_dblstr(stream &s, double d)
{
	if (isnan(d))
		s << "\"NaN\"";
	else if (isinf(d))
		s << "\"Infinity\"";
	else
		s << d;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_bool
 * variant:       parse_ascii_bool_strict
 */
int parse_ascii_bool(bool *v, const char *ascii)
{
	int r = 0;
	if ('0' == ascii[0]) {
		*v = false;
		r = 1;
	} else if ('1' == ascii[0]) {
		*v = true;
		r = 1;
	} else if (0 == memcmp(ascii,"true",4)) {
		*v = true;
		r = 4;
	} else if (0 == memcmp(ascii,"false",5)) {
		*v = false;
		r = 5;
	} else if (0 == memcmp(ascii,"on",2)) {
		*v = true;
		r = 2;
	} else if (0 == memcmp(ascii,"off",3)) {
		*v = false;
		r = 3;
	} else {
		return -14;
	}
	char c = ascii[r];
	if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_'))
		return -15;
	return r;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_dbl
 * variant:       parse_ascii_dbl
 */
int parse_ascii_dbl(double *v, const char *ascii)
{
	char *eptr;
	double d = strtod(ascii,&eptr);
	if (eptr == ascii)
		return -16;
	*v = d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_flt
 * variant:       parse_ascii_flt
 */
int parse_ascii_flt(float *v, const char *ascii)
{
	char *eptr;
	float f = strtof(ascii,&eptr);
	if (eptr == ascii)
		return -17;
	*v = f;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_s8
 * variant:       parse_ascii_s8
 */
int parse_ascii_s8(int8_t *v, const char *ascii)
{
	char *eptr;
	long d = strtol(ascii,&eptr,0);
	if (eptr == ascii)
		return -18;
	if ((d < INT8_MIN) || (d > INT8_MAX))
		return -19;
	*v = (uint8_t) d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_s16
 * variant:       parse_ascii_s16
 */
int parse_ascii_s16(int16_t *v, const char *ascii)
{
	char *eptr;
	long d = strtol(ascii,&eptr,0);
	if (eptr == ascii)
		return -20;
	if ((d < INT16_MIN) || (d > INT16_MAX))
		return -21;
	*v = (uint16_t) d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_s32
 * variant:       parse_ascii_s32
 */
int parse_ascii_s32(int32_t *v, const char *ascii)
{
	char *eptr;
	long long d = strtoll(ascii,&eptr,0);
	if (eptr == ascii)
		return -22;
	if ((d < INT32_MIN) || (d > INT32_MAX))
		return -23;
	*v = (uint32_t) d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_s64
 * variant:       parse_ascii_s64
 */
int parse_ascii_s64(int64_t *v, const char *ascii)
{
	char *eptr;
	unsigned long long d = strtoull(ascii,&eptr,0);
	if (eptr == ascii)
		return -24;
	*v = (int64_t) d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_u8
 * variant:       parse_ascii_u8
 */
int parse_ascii_u8(uint8_t *v, const char *ascii)
{
	char *eptr;
	long d = strtol(ascii,&eptr,0);
	if (eptr == ascii)
		return -25;
	if ((d < 0) || (d > UINT8_MAX))
		return -26;
	*v = (uint8_t) d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_u16
 * variant:       parse_ascii_u16
 */
int parse_ascii_u16(uint16_t *v, const char *ascii)
{
	char *eptr;
	long d = strtol(ascii,&eptr,0);
	if (eptr == ascii)
		return -27;
	if ((d < 0) || (d > UINT16_MAX))
		return -28;
	*v = (uint16_t) d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_u32
 * variant:       parse_ascii_u32
 */
int parse_ascii_u32(uint32_t *v, const char *ascii)
{
	char *eptr;
	long long d = strtoll(ascii,&eptr,0);
	if (eptr == ascii)
		return -29;
	if ((d < 0) || (d > UINT32_MAX))
		return -30;
	*v = (uint32_t) d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_u64
 * variant:       parse_ascii_u64
 */
int parse_ascii_u64(uint64_t *v, const char *ascii)
{
	char *eptr;
	unsigned long long d = strtoull(ascii,&eptr,0);
	if (eptr == ascii)
		return -31;
	*v = (uint64_t) d;
	return eptr-ascii;
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u16
 * variant:       send_u16
 */
void send_u16(void (*put)(uint8_t), uint16_t v)
{
	put(v & 0xff);
	v >>= 8;
	put(v);
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u32
 * variant:       send_u32
 */
void send_u32(void (*put)(uint8_t), uint32_t v)
{
	put(v & 0xff);
	v >>= 8;
	put(v & 0xff);
	v >>= 8;
	put(v & 0xff);
	v >>= 8;
	put(v);
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u64
 * variant:       send_u64_speed
 * Optimize: speed
 */
void send_u64(void (*put)(uint8_t), uint64_t v)
{
	put(v & 0xff);
	v >>= 8;
	put(v & 0xff);
	v >>= 8;
	put(v & 0xff);
	v >>= 8;
	put(v & 0xff);
	v >>= 8;
	put(v & 0xff);
	v >>= 8;
	put(v & 0xff);
	v >>= 8;
	put(v & 0xff);
	v >>= 8;
	put(v);
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_varint
 * variant:       send_varint
 */
void send_varint(void (*put)(uint8_t), varint_t v)
{
	do {
		uint8_t u8 = v & 0x7f;
		v >>= 7;
		if (v)
			u8 |= 0x80;
		put(u8);
	} while (v);
}


/* included from: (WFC_ROOT)/share/xvarint.cc
 * function:      send_xvarint
 * variant:       send_xvarint_32
 * intsize: 32
 */
void send_xvarint(void (*put)(uint8_t), varint_t v)
{
	uint8_t fill64 = (varsint_t)v < 0 ? 0xf0 : 0;
	do {
		uint8_t u8 = v & 0x7f;
		v >>= 7;
		if (v)
			u8 |= 0x80;
		else
			u8 |= fill64;
		put(u8);
	} while (v);
	if (fill64) {
		for (int i = 0; i < 4; ++i)
			put(0xff);
		put(0x1);
	}
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_bytes
 * variant:       send_bytes_0
 */
void send_bytes(void (*put)(uint8_t), const uint8_t *d, unsigned n)
{
	const uint8_t *e = d + n;
	while (d != e) {
		for (unsigned i = 0; i < n; ++i)
			put((uint8_t)*d++);
	}
}


/* included from: (WFC_ROOT)/share/xvarint.cc
 * function:      write_xvarint
 * variant:       write_xvarint_32
 * intsize: 32
 */
int write_xvarint(uint8_t *wire, ssize_t wl, varint_t v)
{
	uint8_t *w = wire;
	int fill64 = (varsint_t)v < 0;
	do {
		uint8_t u8 = v & 0x7f;
		v >>= 7;
		if (v)
			u8 |= 0x80;
		if (--wl <= 0)
			return -32;
		*w++ = u8;
	} while (v);
	if (fill64) {
		w[-1] |= 0xf0;
		for (int i = 0; i < 4; ++i)
			*w++ = 0xff;
		*w++ = 0x1;
	}
	return w-wire;
}


/* included from: (WFC_ROOT)/share/place_varint.cc
 * function:      place_varint
 * variant:       place_varint_vi32
 * VarIntBits: 32
 * optimize: speed
 * description: place
 */
void place_varint(uint8_t *w, varint_t v)
{
	*w++ = (v & 0x7f) | 0x80;
	v >>= 7;
	*w++ = (v & 0x7f) | 0x80;
	v >>= 7;
	*w++ = (v & 0x7f) | 0x80;
	v >>= 7;
	*w++ = (v & 0x7f) | 0x80;
	v >>= 7;
	*w++ = (v & 0x7f) | 0x80;
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u16
 * variant:       send_u16
 */
void send_u16(estring &put, uint16_t v)
{
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v);
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u32
 * variant:       send_u32
 */
void send_u32(estring &put, uint32_t v)
{
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v);
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u64
 * variant:       send_u64_speed
 * Optimize: speed
 */
void send_u64(estring &put, uint64_t v)
{
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v & 0xff);
	v >>= 8;
	put.push_back(v);
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_varint
 * variant:       send_varint
 */
void send_varint(estring &put, varint_t v)
{
	do {
		uint8_t u8 = v & 0x7f;
		v >>= 7;
		if (v)
			u8 |= 0x80;
		put.push_back(u8);
	} while (v);
}


/* included from: (WFC_ROOT)/share/xvarint.cc
 * function:      send_xvarint
 * variant:       send_xvarint_32
 * intsize: 32
 */
void send_xvarint(estring &put, varint_t v)
{
	uint8_t fill64 = (varsint_t)v < 0 ? 0xf0 : 0;
	do {
		uint8_t u8 = v & 0x7f;
		v >>= 7;
		if (v)
			u8 |= 0x80;
		else
			u8 |= fill64;
		put.push_back(u8);
	} while (v);
	if (fill64) {
		for (int i = 0; i < 4; ++i)
			put.push_back(0xff);
		put.push_back(0x1);
	}
}


