/*
 *  Copyright (C) 2020-2021, Thomas Maier-Komor
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

#ifndef JSON_H
#define JSON_H

// This JSON implementation consciously provides only a subset of the JSON
// spec, to consume as little ROM and RAM as possible.
// Intended limitations:
// - no parsing
// - no arrays
// - no formatting variants
// - no checking for invalid characters
// - no string escape sequences

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "stream.h"

class stream;

class JsonObject;
class JsonBool;
class JsonNumber;
class JsonInt;
class JsonString;

class JsonElement
{
	public:
	explicit JsonElement(const char *n, const char *dim = 0)
	: m_name(n)
	, m_next(0)
	, m_dim(dim)
	{ }

	// pure virtual functions causes problems on ESP8266/xtensa toolchain
	// cxx_demangle is linked to iram0, which overflows the segment!
	//virtual ~JsonElement() = 0;
	virtual ~JsonElement();

	virtual void toStream(stream &) const;
	virtual void writeValue(stream &) const;

	virtual JsonObject *toObject()
	{ return 0; }

	virtual JsonString *toString()
	{ return 0; }

	virtual JsonBool *toBool()
	{ return 0; }

	virtual JsonNumber *toNumber()
	{ return 0; }

	void setNext(JsonElement *e)
	{ m_next = e; }

	JsonElement *next() const
	{ return m_next; }

	const char *name() const
	{ return m_name; }

	void setDimension(const char *dim)
	{ m_dim = dim; }

	const char *getDimension() const
	{ return m_dim; }

	protected:
	const char *m_name;
	JsonElement *m_next;
	const char *m_dim;

	private:
	JsonElement(const JsonElement &);		// intentionally not supported
	JsonElement& operator = (const JsonElement &);	// intentionally not supported
};


class JsonBool : public JsonElement
{
	public:
	JsonBool(const char *name, bool v)
	: JsonElement(name)
	, m_value(v)
	{ }

	JsonBool *toBool()
	{ return this; }

	void writeValue(stream &) const;

	void set(bool v)
	{ m_value = v; }

	bool get() const
	{ return m_value; }

	private:
	bool m_value;
};


class JsonNumber : public JsonElement
{
	public:
	explicit JsonNumber(const char *name, const char *dim = 0)
	: JsonElement(name,dim)
	, m_value(NAN)
	{ }

	JsonNumber(const char *name, double v)
	: JsonElement(name)
	, m_value(v)
	{ }

	JsonNumber *toNumber()
	{ return this; }

	bool isValid() const;

	void writeValue(stream &) const;

	void set(float v)
	{ m_value = v; }

	double get() const
	{ return m_value; }

	protected:
	// must be double to be conforming to JSON spec
	double m_value;
};


class JsonString : public JsonElement
{
	public:
	JsonString(const char *name, const char *v);

	JsonString *toString()
	{ return this; }

	void toStream(stream &) const;
	void writeValue(stream &) const;
	void set(const char *v);

	const char *get() const
	{ return m_value; }

	private:
	~JsonString();		// deletion is not supported
	JsonString(const JsonString &);
	JsonString &operator = (const JsonString &);
	char *m_value;
};


class JsonObject : public JsonElement
{
	public:
	JsonObject(const char *n)
	: JsonElement(n)
	, m_childs(0)
	{ }

	JsonObject *toObject()
	{ return this; }

	JsonBool *add(const char *name, bool value);
	JsonNumber *add(const char *name, double value);
	JsonString *add(const char *name, const char *value);
	JsonObject *add(const char *name);
	JsonElement *get(const char *) const;
	JsonElement *first() const
	{ return m_childs; }

	void append(JsonElement *);
	void toStream(stream &) const;

	private:
	JsonElement *m_childs;
};


inline JsonElement::~JsonElement()
{
	abort();	// deletion is not supported
}


JsonElement *ujson_forward(JsonElement *, const char *basename, const char *subname);


#endif
