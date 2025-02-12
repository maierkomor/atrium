/****************************************************************************
 * 
 * Code generated by Wire-Format-Compiler (WFC)
 * WFC Version: P2304.13 (hg:469/ae43a66811cb)
 * WFC is Copyright 2015-2025, Thomas Maier-Komor
 * 
 * Source Information:
 * ===================
 * Filename : hwcfg.wfc
 * Copyright: 2018-2024
 * Author   : Thomas Maier-Komor
 * 
 * Code generated on 2025-02-09, 21:27:14 (CET).
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
 * options from esp8266:
 * Optimize        : "size"
 * 
 * options from esp:
 * bytestype       : "estring"
 * enumnames       : "true"
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
 * BaseClass       : "Message"
 * getMember       : "getMember"
 * 
 * options from defaults:
 * AddPrefix       : "add_"
 * ascii_bool      : "ascii_bool"
 * ascii_bytes     : "ascii_bytes"
 * ascii_indent    : "ascii_indent"
 * ascii_string    : "ascii_string"
 * author          : ""
 * calcSize        : "calcSize"
 * cfgdefs         : ""
 * ClearName       : "clear"
 * ClearPrefix     : "clear_"
 * ClearValue      : "compatible"
 * copyright       : ""
 * email           : ""
 * endian          : "unknown"
 * enummap         : "false"
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
 * disabled flags from esp8266:
 * 	withUnequal
 * enabled flags from esp:
 * 	withEqual
 * enabled flags from common:
 * 	id0
 * disabled flags from defaults:
 * 	debug, SubClasses
 * enabled flags from defaults:
 * 	asserts, comments, genlib, gnux
 */

#ifdef WFC_ENDIAN
#if WFC_ENDIAN != 0
#error WFC generated code incompatible due to endian
#endif
#else
#define WFC_ENDIAN     0 // unknown endian
#endif

#define HAVE_TO_MEMORY 1
#define HAVE_TO_ASCII 1
#define HAVE_TO_JSON 1
#define HAVE_GET_MEMBER 1
#define HAVE_FROM_MEMORY 1
#define ON_ERROR_CANCEL 1
#define HAVE_ENUM_NAMES 1

#ifndef _WFCCORE_ESP8266_H
#define _WFCCORE_ESP8266_H


#include <assert.h>
#define OUTPUT_TO_ASCII 1
#include <iosfwd>
#include <string>
/* std::vector support not needed */
/* array support not needed */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
/* user requested header files */
#include "estring.h"
#include <sdkconfig.h>
#include "stream.h"
#include <map>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>



typedef uint32_t varint_t;
typedef int32_t varsint_t;
typedef _ssize_t ssize_t;


class Message
{
	public:
	virtual ~Message() = 0;
	
	//! calculate the number ob bytes needed for a serialized representation
	virtual size_t calcSize() const = 0;
	
	/*!
	* Function for serializing the object to memory.
	* @param b buffer to serialize the object to
	* @param s number of bytes available in the buffer
	* @return number of bytes successfully serialized
	*/
	virtual ssize_t toMemory(uint8_t *, ssize_t) const = 0;
	
	/*!
	* Function for deserializing an object from memory.
	* @param b buffer to deserialize the object from
	* @param s number of bytes available in the buffer
	* @return number of bytes successfully serialized
	*/
	virtual ssize_t fromMemory(const void *, ssize_t) = 0;
	
	/*!
	* output message as JSON to stream
	* @param json: output stream for JSON
	* @param indLvl: indention level
	*/
	virtual void toJSON(stream &json, unsigned indLvl = 0) const = 0;
	
	/*!
	* output message as ASCII to stream
	* @param out: output stream
	* @param indLvl: indention level
	*/
	virtual void toASCII(stream &out, size_t indLvl = 0) const = 0;
	
	/*!
	* Set a member specified by a parameter to a value parsed from
	* an ASCII argument.
	* @param name: name of member that is to be modified
	* @param value: value string that shall be parsed for setting the member
	*/
	virtual int setByName(const char *name, const char *value) = 0;
	
	/*!
	* Get member or submember object of message type.
	* @param s: start of member name string path
	* @return: pointer to message object of the relevant element
	*/
	Message *getMember(const char *);
	
	protected:
	virtual Message *p_getMember(const char *s, unsigned n)
	{ return 0; }
	virtual Message *p_getMember(const char *s, unsigned n, unsigned x)
	{ return 0; }
};

inline Message::~Message()
{
}


/* included from: (WFC_ROOT)/share/write_varint.cct
 * function:      write_varint
 * variant:       write_varint_generic
 */
extern int write_varint(uint8_t *wire, ssize_t wl, varint_t v);


/* included from: (WFC_ROOT)/share/read_mem.cc
 * function:      read_u64
 * variant:       read_u64_generic
 */
inline uint64_t read_u64(const uint8_t *wire)
{
	uint64_t r;
	r = (uint64_t)*wire++;
	r |= (uint64_t)*wire++ << 8;
	r |= (uint64_t)*wire++ << 16;
	r |= (uint64_t)*wire++ << 24;
	r |= (uint64_t)*wire++ << 32;
	r |= (uint64_t)*wire++ << 40;
	r |= (uint64_t)*wire++ << 48;
	r |= (uint64_t)*wire << 56;
	return r;
}


/* included from: (WFC_ROOT)/share/read_mem.cc
 * function:      read_u32
 * variant:       read_u32_generic
 */
inline uint32_t read_u32(const uint8_t *wire)
{
	uint32_t r;
	r = (uint32_t)*wire++;
	r |= (uint32_t)*wire++ << 8;
	r |= (uint32_t)*wire++ << 16;
	r |= (uint32_t)*wire << 24;
	return r;
}


/* included from: (WFC_ROOT)/share/read_mem.cc
 * function:      read_u16
 * variant:       read_u16_generic
 */
inline uint16_t read_u16(const uint8_t *wire)
{
	uint16_t r;
	r = (uint16_t)wire[0]
	| (uint16_t)wire[1] << 8;
	return r;
}


/* included from: (WFC_ROOT)/share/early_decode.cct
 * function:      decode_union
 * variant:       decode_union
 */
union decode_union
{
	varint_t vi;
	uint64_t u64;
	double d;
	uint32_t u32;
	float f;
	uint16_t u16;
	uint8_t u8;
};


/* included from: (WFC_ROOT)/share/mangle.cc
 * function:      mangle_float
 * variant:       mangle_float_union
 */
union mangle_float
{
	float m_d;
	uint32_t m_u;
	
	mangle_float(float d)
	: m_d(d)
	{ }
	
	operator uint32_t () const
	{ return m_u; }
};


/* included from: (WFC_ROOT)/share/mangle.cc
 * function:      mangle_double
 * variant:       mangle_double_union
 */
union mangle_double
{
	double m_d;
	uint64_t m_u;
	
	mangle_double(double d)
	: m_d(d)
	{ }
	
	operator uint64_t () const
	{ return m_u; }
};


/* included from: (WFC_ROOT)/share/wiresize.cc
 * function:      wiresize
 * variant:       wiresize_generic
 */
extern unsigned wiresize(varint_t u);


/* included from: (WFC_ROOT)/share/wiresize_s.cc
 * function:      wiresize_s
 * variant:       wiresize_s_32
 * varintbits: 32
 */
extern unsigned wiresize_s(int32_t s);


/* included from: (WFC_ROOT)/share/wiresize_x.cc
 * function:      wiresize_x
 * variant:       wiresize_x_generic
 */
extern unsigned wiresize_x(varsint_t v);


/* included from: (WFC_ROOT)/share/sint_varint.cc
 * function:      sint_varint
 * variant:       sint_varint_0
 */
inline varint_t sint_varint(varsint_t i)
{
	return (i << 1) ^ (i >> ((sizeof(varint_t)*8)-1));
}


/* included from: (WFC_ROOT)/share/varint_sint.cc
 * function:      varint_sint
 * variant:       varint_sint_default
 */
inline int64_t varint_sint(varint_t v)
{
	varint_t x = -(v & 1);
	v >>= 1;
	v ^= x;
	return v;
}


/* included from: (WFC_ROOT)/share/write_mem.cc
 * function:      write_u16
 * variant:       write_u16_0
 */
inline void write_u16(uint8_t *wire, uint16_t v)
{
	*wire++ = v & 0xff;
	v >>= 8;
	*wire++ = v;
}


/* included from: (WFC_ROOT)/share/write_mem.cc
 * function:      write_u32
 * variant:       write_u32_0
 */
extern void write_u32(uint8_t *wire, uint32_t v);


/* included from: (WFC_ROOT)/share/write_mem.cc
 * function:      write_u64
 * variant:       write_u64_generic
 */
extern void write_u64(uint8_t *wire, uint64_t v);


/* included from: (WFC_ROOT)/share/early_decode.cct
 * function:      decode_early
 * variant:       decode_early
 */
extern int decode_early(const uint8_t *s, const uint8_t *e, union decode_union *ud, varint_t *fid);


/* included from: (WFC_ROOT)/share/read_varint.cct
 * function:      read_varint
 * variant:       read_varint_default
 */
extern unsigned read_varint(const uint8_t *wire, ssize_t wl, varint_t *r);


/* included from: (WFC_ROOT)/share/read_mem.cc
 * function:      read_double
 * variant:       read_double_generic
 */
extern double read_double(const uint8_t *wire);


/* included from: (WFC_ROOT)/share/read_mem.cc
 * function:      read_float
 * variant:       read_float_generic
 */
inline float read_float(const uint8_t *wire)
{
	union { uint32_t i; float f; } r;
	r.i = (uint32_t)*wire++;
	r.i |= (uint32_t)*wire++ << 8;
	r.i |= (uint32_t)*wire++ << 16;
	r.i |= (uint32_t)*wire << 24;
	return r.f;
}


/* included from: (WFC_ROOT)/share/skip_content.cct
 * function:      skip_content
 * variant:       skip_content
 */
extern ssize_t skip_content(const uint8_t *wire, ssize_t wl, unsigned type);


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_indent
 * variant:       ascii_indent
 */
extern void ascii_indent(stream &out, ssize_t n, const char *fname = 0);


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_bool
 * variant:       ascii_bool
 */
extern void ascii_bool(stream &out, ssize_t n, const char *fname, bool v);


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_bytes
 * variant:       ascii_bytes
 */
extern void ascii_bytes(stream &out, const uint8_t *str, size_t len, size_t indent, const char *fname = 0);


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_string
 * variant:       ascii_string
 */
extern void ascii_string(stream &out, size_t indent, const char *str, size_t len, const char *fname = 0);


/* included from: (WFC_ROOT)/share/ascii_funcs.cc
 * function:      ascii_numeric
 * variant:       ascii_numeric
 */
template <typename T>
void ascii_numeric(stream &out, ssize_t n, const char *fname, T v)
{
	out.put('\n');
	while (n > 0) {
		out.put('\t');
		--n;
	}
	if (fname) {
		out << fname;
		out << " = ";
	}
	out << v;
	out << ';';
}


/* included from: (WFC_ROOT)/share/json_string.cct
 * function:      json_string
 * variant:       json_string
 */
template <class C>
void json_string(stream &json, const C &str)
{
	json.put('"');
	const char *data = (const char *)str.data();
	size_t s = str.size();
	while (s) {
		char c = (char) *data++;
		if ((c >= 0x20) && (c <= 0x7e)) {
			json.put(c);
		} else {
			json.put('\\');
			switch (c) {
			case '\n':
				json.put('n');
				break;
			case '\r':
				json.put('r');
				break;
			case '\b':
				json.put('b');
				break;
			case '\f':
				json.put('f');
				break;
			case '\t':
				json.put('t');
				break;
			case '\\':
			case '"':
			case '/':
				json.put(c);
				break;
			default:
				{
					char buf[8];
					// double-cast needed
					// for range 0..255
						int n = sprintf(buf,"u%04x",(unsigned)(uint8_t)c);
					json.write(buf,n);
				}
			}
		}
		--s;
	}
	json.put('"');
}


/* included from: (WFC_ROOT)/share/json_string.cct
 * function:      json_indent
 * variant:       json_indent
 */
template <typename streamtype>
char json_indent(streamtype &json, unsigned indLvl, char fsep, const char *fname = 0)
{
	if (fsep) {
		json.put(fsep);
		json.put('\n');
	}
	while (indLvl) {
		--indLvl;
		json.write("  ",2);
	}
	if (fname) {
		json.put('"');
		json << fname;
		json.write("\":",2);
	}
	return ',';
}


/* included from: (WFC_ROOT)/share/json_string.cct
 * function:      json_cstr
 * variant:       json_cstr_direct
 */
extern void json_cstr(stream &json, const char *cstr);


/* included from: (WFC_ROOT)/share/json_string.cct
 * function:      to_decstr
 * variant:       to_decstr
 */
template <typename T>
void to_decstr(stream &s, T t)
{
	s << t;
}


/* included from: (WFC_ROOT)/share/json_string.cct
 * function:      to_dblstr
 * variant:       to_dblstr_str
 */
extern void to_dblstr(stream &s, double d);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_bool
 * variant:       parse_ascii_bool_strict
 */
extern int parse_ascii_bool(bool *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_bytes
 * variant:       parse_ascii_bytes
 */
template <typename T>
int parse_ascii_bytes(T &b, const char *ascii)
{
	const char *at = ascii;
	while (*at) {
		char c0 = *at++;
		while ((c0 == ' ') || (c0 == '\t') || (c0 == '\n') || (c0 == '\r'))
			c0 = *at++;
		char c1 = *at++;
		while ((c1 == ' ') || (c1 == '\t') || (c1 == '\n') || (c1 == '\r'))
			c1 = *at++;
		if (c1 == 0)
			return -1;
		uint8_t v;
		if ((c0 >= '0') && (c0 <= '9'))
			v = c0-'0';
		else if ((c0 >= 'a') && (c0 <= 'f'))
			v = c0-'a'+10;
		else if ((c0 >= 'A') && (c0 <= 'F'))
			v = c0-'A'+10;
		else
			return at-ascii-1;
		v <<= 4;
		if ((c1 >= '0') && (c1 <= '9'))
			v |= c1-'0';
		else if ((c1 >= 'a') && (c1 <= 'f'))
			v |= c1-'a'+10;
		else if ((c1 >= 'A') && (c1 <= 'F'))
			v |= c1-'A'+10;
		else
			return -2;
		b.push_back(v);
	}
	return at-ascii;
}


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_dbl
 * variant:       parse_ascii_dbl
 */
extern int parse_ascii_dbl(double *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_flt
 * variant:       parse_ascii_flt
 */
extern int parse_ascii_flt(float *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_s8
 * variant:       parse_ascii_s8
 */
extern int parse_ascii_s8(int8_t *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_s16
 * variant:       parse_ascii_s16
 */
extern int parse_ascii_s16(int16_t *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_s32
 * variant:       parse_ascii_s32
 */
extern int parse_ascii_s32(int32_t *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_s64
 * variant:       parse_ascii_s64
 */
extern int parse_ascii_s64(int64_t *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_u8
 * variant:       parse_ascii_u8
 */
extern int parse_ascii_u8(uint8_t *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_u16
 * variant:       parse_ascii_u16
 */
extern int parse_ascii_u16(uint16_t *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_u32
 * variant:       parse_ascii_u32
 */
extern int parse_ascii_u32(uint32_t *v, const char *ascii);


/* included from: (WFC_ROOT)/share/parse_ascii.cct
 * function:      parse_ascii_u64
 * variant:       parse_ascii_u64
 */
extern int parse_ascii_u64(uint64_t *v, const char *ascii);


/* included from: (WFC_ROOT)/share/decode.cct
 * function:      decode_bytes
 * variant:       decode_bytes
 */
template <typename S>
int decode_bytes(S &s, const uint8_t *a, const uint8_t *e)
{
	varint_t v;
	int n = read_varint(a,e-a,&v);
	a += n;
	if ((n <= 0) || ((a+v) > e))
		return -1;
	s.assign((const char *)a,v);
	return n+v;
}


/* included from: (WFC_ROOT)/share/decode.cct
 * function:      decode_bytes_element
 * variant:       decode_bytes_element
 */
template <typename V>
int decode_bytes_element(V &s, const uint8_t *a, const uint8_t *e)
{
	varint_t v;
	int n = read_varint(a,e-a,&v);
	a += n;
	if ((n <= 0) || ((a+v) > e))
		return -1;
	s.emplace_back((const char *)a,v);
	return n+v;
}


/* included from: (WFC_ROOT)/share/encode.cc
 * function:      encode_bytes
 * variant:       encode_bytes
 */
template<typename S>
int encode_bytes(const S &s, uint8_t *a, uint8_t *e)
{
	ssize_t l = s.size();
	int n = write_varint(a,e-a,l);
	a += n;
	if ((n <= 0) || ((e-a) < l))
		return -1;
	memcpy(a,s.data(),l);
	return l + n;
}


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u16
 * variant:       send_u16
 */
extern void send_u16(void (*put)(uint8_t), uint16_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u32
 * variant:       send_u32
 */
extern void send_u32(void (*put)(uint8_t), uint32_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u64
 * variant:       send_u64_size
 * Optimize: size
 */
extern void send_u64(void (*put)(uint8_t), uint64_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_varint
 * variant:       send_varint
 */
extern void send_varint(void (*put)(uint8_t), varint_t v);


/* included from: (WFC_ROOT)/share/xvarint.cc
 * function:      send_xvarint
 * variant:       send_xvarint_32
 * intsize: 32
 */
extern void send_xvarint(void (*put)(uint8_t), varint_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_bytes
 * variant:       send_bytes_0
 */
extern void send_bytes(void (*put)(uint8_t), const uint8_t *d, unsigned n);


/* included from: (WFC_ROOT)/share/xvarint.cc
 * function:      write_xvarint
 * variant:       write_xvarint_32
 * intsize: 32
 */
extern int write_xvarint(uint8_t *wire, ssize_t wl, varint_t v);


/* included from: (WFC_ROOT)/share/place_varint.cc
 * function:      place_varint
 * variant:       place_varint_generic
 * description: place
 */
extern void place_varint(uint8_t *w, varint_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u16
 * variant:       send_u16
 */
extern void send_u16(estring &put, uint16_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u32
 * variant:       send_u32
 */
extern void send_u32(estring &put, uint32_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_u64
 * variant:       send_u64_size
 * Optimize: size
 */
extern void send_u64(estring &put, uint64_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_varint
 * variant:       send_varint
 */
extern void send_varint(estring &put, varint_t v);


/* included from: (WFC_ROOT)/share/xvarint.cc
 * function:      send_xvarint
 * variant:       send_xvarint_32
 * intsize: 32
 */
extern void send_xvarint(estring &put, varint_t v);


/* included from: (WFC_ROOT)/share/send_data.cct
 * function:      send_msg
 * variant:       send_msg
 */
template <typename S, class C>
void send_msg(S &str, const C &c)
{
	std::string tmp;
	c.toString(tmp);
	send_varint(str,tmp.size());
	str.append(tmp);
}


#endif
