/*
 *  Copyright (C) 2020, Thomas Maier-Komor
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

#ifndef DATAFLOW_H
#define DATAFLOW_H

#include <stdint.h>

class Function;


class DataSignal
{
	public:
	virtual class BoolSignal *toBoolSignal()
	{ return 0; }

	virtual class FloatSignal *toFloatSignal()
	{ return 0; }

	virtual class IntSignal *toIntSignal()
	{ return 0; }

	virtual const char *signalType() const
	{ return 0; }

	const char *signalName() const
	{ return m_name; }

	const char *signalDimension() const
	{ return m_dim; }

	DataSignal *getNext()
	{ return m_next; }

	virtual bool isValid() const
	{ return true; }

	virtual void toStream(class stream &);
	virtual int initFrom(const char *);
	void addFunction(Function *);

	static DataSignal *getSignal(const char *);
	static DataSignal *first();

	protected:
	DataSignal(const char *name, const char *dim = "");
	void process();	// must be called after updating the signal value

//	~DataSignal();
	const char *m_name;
	const char *m_dim = "";

	private:
	DataSignal *m_next = 0;
};


/*
class BoolSignal : public DataSignal
{
	public:
	BoolSignal(const char *name, const char *src, bool v = false)
	: DataSignal(name,src)
	, m_value(v)
	{ }

	BoolSignal *toBoolSignal()
	{ return this; }

	const char *signalType() const
	{ return "bool"; }

	bool getValue() const
	{ return m_value; }

	void setValue(bool v);
	void toStream(class stream &);

	private:
	bool m_value;
};
*/


class FloatSignal : public DataSignal
{
	public :
	FloatSignal(const char *name, float v = 0)
	: DataSignal(name)
	, m_value(v)
	{ }

	FloatSignal *toFloatSignal()
	{ return this; }

	const char *signalType() const
	{ return "float"; }

	double getValue() const
	{ return m_value; }

	int initFrom(const char *);
	void setValue(float v);
	void toStream(class stream &);
	bool isValid() const;

	private:
	float m_value;
};


class IntSignal : public DataSignal
{
	public:
	IntSignal(const char *name, int64_t v = 0)
	: DataSignal(name)
	, m_value(v)
	{ }

	IntSignal *toIntSignal()
	{ return this; }

	const char *signalType() const
	{ return "int"; }

	int64_t getValue() const
	{ return m_value; }

	int initFrom(const char *);
	void setValue(int32_t v);
	void toStream(class stream &);

	private:
	int32_t m_value;
};


#endif
