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

#ifndef FUNC_H
#define FUNC_H

#include "event.h"

class DataSignal;
class IntSignal;


class Function
{
	public:
	virtual ~Function() = default;
	virtual const char *type() const;

	virtual void operator () (DataSignal *);

	static Function *getInstance(const char *);

	static Function *first()
	{ return First; }

	const char *name() const
	{ return m_name; }

	Function *next() const
	{ return m_next; }

	//virtual char **paramNames() const;
	virtual int setParam(unsigned x, DataSignal *s);
	//void setParam(const char *str, DataSignal *s);
	DataSignal *getParam(const char *);
	virtual int addParam(const char *);

	protected:
	explicit Function(const char *name);

	const char *m_name;

	private:
	static Function *First;
	Function *m_next = 0;
	Function *m_callchain = 0;
	friend DataSignal;
};


class FunctionFactory
{
	public:
	FunctionFactory();

	virtual const char *name() const;
	
	virtual Function *create(const char *) const;

	static Function *create(const char *func, const char *name);

	static FunctionFactory *first()
	{ return Factories; }

	FunctionFactory *next()
	{ return m_next; }

	private:
	static FunctionFactory *Factories;
	FunctionFactory *m_next;
};


template <class C>
struct FuncFact : public FunctionFactory
{
	FuncFact()
	: FunctionFactory()
	{ }

	const char *name() const
	{ return C::FuncName; }

	Function *create(const char *name) const
	{ return new C(name); }
};


class BinaryOperator : public Function
{
	public:
	BinaryOperator(const char *);

	int setParam(unsigned x, DataSignal *s);
	int addParam(const char *);

	protected:
	IntSignal *m_result;
	DataSignal *m_left, *m_right;
};


void fn_init_factories();


#endif
