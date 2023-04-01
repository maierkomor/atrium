/*
 *  Copyright (C) 2020-2022, Thomas Maier-Komor
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

#ifndef ENV_H
#define ENV_H

// This JSON implementation consciously provides only a subset of the JSON
// spec, to consume as little ROM and RAM as possible.
// Intended limitations:
// - no parsing
// - no arrays
// - no formatting variants
// - no checking for invalid characters
// - no string escape sequences

#include "event.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

class stream;

class EnvObject;
class EnvBool;
class EnvNumber;
class EnvInt;
class EnvString;


class EnvElement
{
	public:
	// pure virtual functions causes problems on ESP8266/xtensa toolchain
	// cxx_demangle is linked to iram0, which overflows the segment!
	//virtual ~EnvElement() = 0;
	virtual ~EnvElement();

	virtual void toStream(stream &) const;
	virtual void writeValue(stream &) const;

	virtual EnvObject *toObject()
	{ return 0; }

	virtual EnvString *toString()
	{ return 0; }

	virtual EnvBool *toBool()
	{ return 0; }

	virtual EnvNumber *toNumber()
	{ return 0; }

	virtual unsigned numElements() const
	{ return 1; }

	const char *name() const
	{ return m_name; }

	const char *getDimension() const
	{ return m_dim; }

	void setDimension(const char *dim);

	void setName(char *n);

	EnvObject *getParent() const
	{ return m_parent; }

	protected:
	explicit EnvElement(const char *n, const char *dim = 0);

	char *m_name;
	char *m_dim;
	friend class EnvObject;
	EnvObject *m_parent = 0;

	private:
	EnvElement(const EnvElement &);		// intentionally not supported
	EnvElement& operator = (const EnvElement &);	// intentionally not supported
};


class EnvBool : public EnvElement
{
	public:
	EnvBool(const char *name, bool v, const char *dim = 0)
	: EnvElement(name,dim)
	, m_value(v)
	{ }

	EnvBool *toBool()
	{ return this; }

	void writeValue(stream &) const;

	void set(bool v)
	{ m_value = v; }

	bool get() const
	{ return m_value; }

	private:
	bool m_value;
};



class EnvNumber : public EnvElement
{
	public:
	explicit EnvNumber(const char *name, const char *dim = 0, const char *fmt = 0)
	: EnvElement(name,dim)
	, m_value(NAN)
	{
		if (fmt)
			m_fmt = fmt;
	}

	EnvNumber(const char *name, double v, const char *dim = 0, const char *fmt = 0)
	: EnvElement(name,dim)
	, m_value(v)
	{
		if (fmt)
			m_fmt = fmt;
	}

	EnvNumber *toNumber() override
	{ return this; }

	void set(float v);

	int setThresholds(float lo, float hi, const char *n = 0);

	void writeValue(stream &) const;

	float get() const
	{ return m_value; }

	bool isValid() const
//	{ return isnormal(m_value); }
	{ return !isnan(m_value); }

	void setFormat(const char *f)
	{ m_fmt = f; }

	const char *getFormat() const
	{ return m_fmt; }

	float getHigh() const
	{ return m_high; }

	float getLow() const
	{ return m_low; }

	protected:
	// must be double to be conforming to JSON spec
	float m_value;
	float m_high = NAN, m_low = NAN;
	event_t m_evhi = 0, m_evlo = 0;
	int8_t m_tst = 0;
	const char *m_fmt = "%4.1f";
};


class EnvString : public EnvElement
{
	public:
	EnvString(const char *name, const char *v, const char *dim = 0);
	~EnvString();		// deletion is not supported

	EnvString *toString()
	{ return this; }

	void toStream(stream &) const;
	void writeValue(stream &) const;
	void set(const char *v);
	void set(const char *v, size_t l);

	const char *get() const
	{ return m_value; }

	private:
	EnvString(const EnvString &);
	EnvString &operator = (const EnvString &);
	char *m_value;
};


class EnvObject : public EnvElement
{
	public:
	explicit EnvObject(const char *n)
	: EnvElement(n)
	{ }

	EnvObject *toObject()
	{ return this; }

	EnvBool *add(const char *name, bool value, const char *dim = 0);
	EnvNumber *add(const char *name, double value, const char *dim = 0, const char *fmt = 0);
	EnvString *add(const char *name, const char *value, const char *dim = 0);
	EnvObject *add(const char *name);
	void add(EnvElement *e)
	{ append(e); }
	void remove(EnvElement *e);
	EnvElement *get(const char *) const;
	EnvElement *getChild(const char *n) const;
	int getOffset(const char *) const;
	int getIndex(const char *n) const;
	EnvElement *find(const char *) const;
	EnvElement *getByPath(const char *n) const;
	EnvObject *getObject(const char *n) const;
	EnvElement *getElement(unsigned i) const;
	unsigned numElements() const override;

	EnvElement *getChild(unsigned i) const
	{
		if (i >= m_childs.size())
			return 0;
		return m_childs[i];
	}

	int getIndex(EnvElement *e) const
	{
		int idx = 0;
		for (EnvElement *c : m_childs) {
			if (e == c)
				return idx;
			++idx;
		}
		return -1;
	}

	size_t numChildren() const
	{ return m_childs.size(); }

	std::vector<EnvElement *> &getChilds()
	{ return m_childs; }

	void toStream(stream &) const;

	private:
	void append(EnvElement *);

	std::vector<EnvElement *> m_childs;
};


EnvElement *ujson_forward(EnvElement *, const char *basename, const char *subname);


#endif
