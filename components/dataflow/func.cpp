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

#include "actions.h"
#include "dataflow.h"
#include "event.h"
#include "func.h"
#include "log.h"
#include "stream.h"

#include <stdlib.h>
#include <string.h>

#include <esp_system.h>


using namespace std;

static char TAG[] = "func";
Function *Function::First = 0;

FunctionFactory *FunctionFactory::Factories = 0;


int Function::addParam(const char *p)
{
	log_dbug(TAG,"function %s: param %s ignored",m_name,p);
	return 1;
}


FunctionFactory::FunctionFactory()
: m_next(Factories)
{
	Factories = this;
}


Function *FunctionFactory::create(const char *func, const char *name)
{
	FunctionFactory *f = Factories;
	while (f) {
		const char *n = f->name();
		if (0 == strcmp(n,func))
			return f->create(name);
		f = f->m_next;
	}
	return 0;
}

const char *FunctionFactory::name() const
{
	abort();
	return 0;
}


Function *FunctionFactory::create(const char *) const
{
	abort();
	return 0;
}


int BinaryOperator::addParam(const char *p)
{
	int r = 0;
	DataSignal *s = DataSignal::getSignal(p);
	if (s == 0) {
		log_warn(TAG,"no signal named %s for function %s",p,m_name);
		r = 1;
	} else if (m_left == 0) {
		m_left = s;
		s->addFunction(this);
		log_dbug(TAG,"set left of %s to %s",m_name,p);
	} else if (m_right == 0) {
		m_right = s;
		s->addFunction(this);
		log_dbug(TAG,"set right of %s to %s",m_name,p);
	} else {
		log_warn(TAG,"parameters of functions %s already set",m_name);
		r = 1;
	}
	return r;
}


class FnLess : public BinaryOperator
{
	public:
	FnLess(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnEqual : public BinaryOperator
{
	public:
	FnEqual(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnLessEqual : public BinaryOperator
{
	public:
	FnLessEqual(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnBinaryAnd : public BinaryOperator
{
	public:
	FnBinaryAnd(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnBinaryOr : public BinaryOperator
{
	public:
	FnBinaryOr(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnBinaryXor : public BinaryOperator
{
	public:
	FnBinaryXor(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnLogicalAnd : public BinaryOperator
{
	public:
	FnLogicalAnd(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnLogicalOr : public BinaryOperator
{
	public:
	FnLogicalOr(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class IntHysteresis : public Function
{
	public:
	IntHysteresis(const char *name, int64_t lo, int64_t hi, bool iv = false);

	const char *type() const
	{ return FuncName; }

	static Function *create(const char *, int argc, const char *args[]);

	void operator() (DataSignal *);

	static const char FuncName[];

	private:
	int64_t m_lo,m_hi;
	bool m_state;
	event_t m_fallev, m_raiseev;
};


class FnAdd: public BinaryOperator
{
	public:
	FnAdd(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnSub: public BinaryOperator
{
	public:
	FnSub(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnDiv: public BinaryOperator
{
	public:
	FnDiv(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnMul : public BinaryOperator
{
	public:
	FnMul(const char *name)
	: BinaryOperator(name)
	{ }

	void operator () (DataSignal *);

	const char *type() const
	{ return FuncName; }
	
	static const char FuncName[];
};


class FnRandom : public Function
{
	public:
	FnRandom(const char *name, IntSignal *lo = 0, IntSignal *hi = 0);

	const char *type() const
	{ return FuncName; }

	static Function *create(const char *, int argc, const char *args[]);

	void operator() (DataSignal *);

	static const char FuncName[];

	private:
	IntSignal *m_lo, *m_hi, m_result;
};


class DelayedTrigger : public Function
{
	public:
	DelayedTrigger(const char *name, DataSignal *);

	static Function *create(const char *, int argc, const char *args[]);

	void operator() (DataSignal *);

	private:
	int64_t m_td;
	event_t m_trigger;
};


void fn_init_factories()
{
	new FuncFact<FnLess>;
	new FuncFact<FnLessEqual>;
	new FuncFact<FnEqual>;
	new FuncFact<FnBinaryAnd>;
	new FuncFact<FnBinaryOr>;
	new FuncFact<FnBinaryXor>;
	new FuncFact<FnRandom>;
	new FuncFact<FnAdd>;
	new FuncFact<FnSub>;
	new FuncFact<FnMul>;
	new FuncFact<FnDiv>;
}


static void exe_func(void *a)
{
	Function *f = (Function *)a;
	f->operator () (0);
}


Function::Function(const char *name)
: m_name(strdup(name))
, m_next(First)
{
	log_dbug(TAG,"adding %s",name);
	First = this;
	action_add(concat("func!",name),exe_func,(void*)this,"execute the function");
}

/*
Function::~Function()
{

}
*/


Function *Function::getInstance(const char *name)
{
	Function *f = First;
	while (f && strcmp(name,f->m_name))
		f = f->m_next;
	return f;
}


BinaryOperator::BinaryOperator(const char *n)
: Function(n)
, m_result(new IntSignal(concat(n,".result")))
, m_left(0)
, m_right(0)
{

}


int Function::setParam(unsigned x, DataSignal *s)
{
	log_warn(TAG,"no implementatior for setParam of %s",m_name);
	return 1;
}


int BinaryOperator::setParam(unsigned x, DataSignal *s)
{
	s->addFunction(this);
	switch (x) {
	case 0:
		m_left = s;
		s->addFunction(this);
		break;
	case 1:
		m_right = s;
		s->addFunction(this);
		break;
	default:
		return 1;
	}
	if (m_left && m_right)
		operator () (m_left);
	return 0;
}


void FnLess::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0))
		return;
	if (IntSignal *ls = m_left->toIntSignal()) {
		int64_t lv = ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv < rs->getValue());
		else if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue((double)lv < rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else if (FloatSignal *ls = m_left->toFloatSignal()) {
		double lv = ls->getValue();
		if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue(lv < rs->getValue());
		else if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv < (double)rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnEqual::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0))
		return;
	if (IntSignal *ls = m_left->toIntSignal()) {
		int64_t lv = ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv == rs->getValue());
		else if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue((double)lv == rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else if (FloatSignal *ls = m_left->toFloatSignal()) {
		double lv = ls->getValue();
		if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue(lv == rs->getValue());
		else if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv == (double)rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnLessEqual::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0))
		return;
	if (IntSignal *ls = m_left->toIntSignal()) {
		int64_t lv = ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv <= rs->getValue());
		else if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue((double)lv <= rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else if (FloatSignal *ls = m_left->toFloatSignal()) {
		double lv = ls->getValue();
		if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue(lv <= rs->getValue());
		else if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv <= (double)rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnBinaryAnd::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0)) {
		log_warn(TAG,"missing parameter in function %s %p %p",m_name,m_left,m_right);
		return;
	}
	if (IntSignal *ls = m_left->toIntSignal()) {
		uint64_t lv = (uint64_t)ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal()) {
			log_dbug(TAG,"and result %x",lv ^ (uint64_t)rs->getValue());
			m_result->setValue(lv & (uint64_t)rs->getValue());
		} else {
			log_warn(TAG,"%s: invalid param 1",m_name);
		}
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnBinaryOr::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0)) {
		log_warn(TAG,"missing parameter in function %s %p %p",m_name,m_left,m_right);
		return;
	}
	if (IntSignal *ls = m_left->toIntSignal()) {
		uint64_t lv = (uint64_t)ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal()) {
			log_dbug(TAG,"or result %x",lv ^ (uint64_t)rs->getValue());
			m_result->setValue(lv | (uint64_t)rs->getValue());
		} else {
			log_warn(TAG,"%s: invalid param 1",m_name);
		}
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnBinaryXor::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0)) {
		log_warn(TAG,"missing parameter in function %s %p %p",m_name,m_left,m_right);
		return;
	}
	if (IntSignal *ls = m_left->toIntSignal()) {
		uint64_t lv = (uint64_t)ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal()) {
			log_dbug(TAG,"xor result %x",lv ^ (uint64_t)rs->getValue());
			m_result->setValue(lv ^ (uint64_t)rs->getValue());
		} else {
			log_warn(TAG,"%s: invalid param 1",m_name);
		}
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnAdd::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0))
		return;
	if (IntSignal *ls = m_left->toIntSignal()) {
		int64_t lv = ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv + rs->getValue());
		else if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue((double)lv + rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else if (FloatSignal *ls = m_left->toFloatSignal()) {
		double lv = ls->getValue();
		if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue(lv + rs->getValue());
		else if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv + (double)rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnSub::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0))
		return;
	if (IntSignal *ls = m_left->toIntSignal()) {
		int64_t lv = ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv - rs->getValue());
		else if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue((double)lv - rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else if (FloatSignal *ls = m_left->toFloatSignal()) {
		double lv = ls->getValue();
		if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue(lv - rs->getValue());
		else if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv - (double)rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnDiv::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0))
		return;
	if (IntSignal *ls = m_left->toIntSignal()) {
		int64_t lv = ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv - rs->getValue());
		else if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue((double)lv - rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else if (FloatSignal *ls = m_left->toFloatSignal()) {
		double lv = ls->getValue();
		if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue(lv - rs->getValue());
		else if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv - (double)rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


void FnMul::operator () (DataSignal *)
{
	if ((m_left == 0) || (m_right == 0))
		return;
	if (IntSignal *ls = m_left->toIntSignal()) {
		int64_t lv = ls->getValue();
		if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv - rs->getValue());
		else if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue((double)lv - rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else if (FloatSignal *ls = m_left->toFloatSignal()) {
		double lv = ls->getValue();
		if (FloatSignal *rs = m_right->toFloatSignal())
			m_result->setValue(lv - rs->getValue());
		else if (IntSignal *rs = m_right->toIntSignal())
			m_result->setValue(lv - (double)rs->getValue());
		else
			log_warn(TAG,"%s: invalid param 1",m_name);
	} else
		log_warn(TAG,"%s: invalid param 0",m_name);
}


FnRandom::FnRandom(const char *name, IntSignal *lo, IntSignal *hi)
: Function(name)
, m_lo(lo)
, m_hi(hi)
, m_result(concat(name,".result"))
{

}


void FnRandom::operator () (DataSignal *)
{
	uint32_t lo = 0, hi = UINT32_MAX;
	if (m_lo != 0) {
		if (IntSignal *l = m_lo->toIntSignal()) 
			lo = l->getValue();
	}
	if (m_hi != 0) {
		if (IntSignal *h = m_hi->toIntSignal()) 
			hi = h->getValue();
	}
	uint32_t r = esp_random();
	r %= (hi-lo);
	r += lo;
	m_result.setValue(r);
}


const char *Function::type() const
{
	return 0;
}


void Function::operator () (DataSignal *)
{
	abort();
}


IntHysteresis::IntHysteresis(const char *name, int64_t lo, int64_t hi, bool iv)
: Function(name)
, m_lo(lo)
, m_hi(hi)
, m_state(iv)
{
	m_fallev = event_register(name,"`fall");
	m_raiseev = event_register(name,"`raise");

}


Function *IntHysteresis::create(const char *name, int argc, const char *args[])
{
	if (argc < 2)
		return 0;
	char *e;
	long lo = strtol(args[0],&e,0);
	if (e == args[0]) {
		log_error(TAG,"argument 1 ist not an integer");
		return 0;
	}
	long hi = strtol(args[1],&e,0);
	if (e == args[1]) {
		log_error(TAG,"argument 2 ist not an integer");
		return 0;
	}
	bool iv = false;
	if (argc > 2) {
		if (!strcmp(args[2],"true"))
			iv = true;
		else if (strcmp(args[2],"false")) {
			log_error(TAG,"not a bool arg");
			return 0;
		}
	}
	return new IntHysteresis(name,lo,hi,iv);
}


void IntHysteresis::operator () (DataSignal *s)
{
	IntSignal *i = s->toIntSignal();
	if (i == 0)
		return;
	int64_t v = i->getValue();
	if (m_state) {
		if (v < m_lo) {
			event_trigger(m_fallev);
			m_state = false;
		}
	} else {
		if (v > m_hi) {
			event_trigger(m_raiseev);
			m_state = true;
		}
	}
}

const char FnLess::FuncName[] = "<";
const char FnLessEqual::FuncName[] = "<=";
const char FnEqual::FuncName[] = "==";
const char FnBinaryAnd::FuncName[] = "and";
const char FnBinaryOr::FuncName[] = "or";
const char FnBinaryXor::FuncName[] = "xor";
const char FnRandom::FuncName[] = "random";
const char FnAdd::FuncName[] = "+";
const char FnSub::FuncName[] = "-";
const char FnDiv::FuncName[] = "/";
const char FnMul::FuncName[] = "*";
