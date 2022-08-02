/*
 *  Copyright (C) 2022, Thomas Maier-Komor
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

#ifndef PARSER_H
#define PARSER_H

#include "arith.tab.h"

#if !defined CONFIG_IDF_TARGET_ESP32 && !defined CONFIG_IDF_TARGET_ESP8266
#include <string>
#include <math.h>
#include <float.h>
#include <iostream>

using namespace std;


struct EnvNumber
{
	explicit EnvNumber(const char *n, float v = NAN)
	: m_name(n)
	, m_val(v)
	, m_next(First)
	{
		First = this;
		cout << "create " << m_name << endl;
	}

	float get() const
	{
		cout << "get " << m_name << endl;
		return m_val;
	}

	void set(float v)
	{
		cout << "set " << m_name << " = " << v << endl;
		m_val = v;
	}

	static EnvNumber *getVar(const char *n)
	{
		EnvNumber *v = First;
		while (v) {
			if (v->m_name == n)
				return v;
			v = v->m_next;
		}
		return new EnvNumber(n);
	}

	private:
	std::string m_name;
	float m_val;
	EnvNumber *m_next = 0;
	static EnvNumber *First;
};
#endif

int yylex(yy::ScriptParser::semantic_type *yylval_param, ScriptDriver &drv);

class ScriptDriver
{
	public:
	int parse(const char *, size_t);

	int lex(yy::ScriptParser::semantic_type *yylval_param);

	bool hadError() const
	{ return m_error; }

	void setError()
	{ m_error = true; }

	private:
	bool m_error = false;
	void *m_scanner = 0;
};

typedef float (*fn1arg_t)(float);
typedef float (*fn2arg_t)(float,float);

fn1arg_t fn1arg_get(const char *);
fn2arg_t fn2arg_get(const char *);

EnvNumber *getvar(const char *);
float eval1(unsigned, float);
float eval2(unsigned, float, float);

#endif
