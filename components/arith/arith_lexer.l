%pointer
%option bison-bridge

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
//FLOATCONST	[0-9]+(.[0-9]*([Ee]-?[0-9]\+)?)?
 */


%{

#include "arith.tab.h"
#include "parser.h"
#include <iostream>
#include <string>

using namespace std;

#define YYSTYPE yy::ScriptParser::semantic_type
#define YY_EXTRA_TYPE ScriptDriver*

%}

AND		"&"
ASSIGN		":="
OR		"|"
NOT		"!"
POWER		"^"
MINUS		"-"
PLUS		"+"
MUL		"*"
DIV		"/"
EQ		"=="
UNEQ		"!="
GT		">"
GTEQ		">="
LT		"<"
LTEQ		"<="
QUEST		"?"
LPAREN		"("
RPAREN		")"
COLON		":"
FLOATCONST	[0-9]+(\.[0-9]*([eE][0-9]+)?)?
IDENTIFIER	[a-zA-Z_][a-zA-Z0-9_]*
FUNC		\$[a-z]+
NL		"\n"

%%

{FLOATCONST}	{
			yylval_param->f = strtof(yytext,0);
//			cout << "value " << yylval_param->f << endl;
			return yy::ScriptParser::token::FLOAT;
		}
{IDENTIFIER}	{
			yylval_param->var = EnvNumber::getVar(yytext);
			return yy::ScriptParser::token::IDENT;
		}
{FUNC}		{
			yylval_param->fn1arg = fn1arg_get(yytext+1);
			if (yylval_param->fn1arg)
				return yy::ScriptParser::token::FUNC1ARG;
			yylval_param->fn2arg = fn2arg_get(yytext+1);
			if (yylval_param->fn2arg)
				return yy::ScriptParser::token::FUNC2ARG;
			return yy::ScriptParser::token::NL;
		}
[ \t\r]	{ }
{NL}		{ return yy::ScriptParser::token::NL; }
{AND}		{ return yy::ScriptParser::token::AND; }
{ASSIGN}	{ return yy::ScriptParser::token::ASSIGN; }
{OR}		{ return yy::ScriptParser::token::OR; }
{PLUS}		{ return yy::ScriptParser::token::PLUS; }
{MINUS}		{ return yy::ScriptParser::token::MINUS; }
{MUL}		{ return yy::ScriptParser::token::MUL; }
{DIV}		{ return yy::ScriptParser::token::DIV; }
{NOT}		{ return yy::ScriptParser::token::NOT; }
{GT}		{ return yy::ScriptParser::token::GT; }
{LT}		{ return yy::ScriptParser::token::LT; }
{GTEQ}		{ return yy::ScriptParser::token::GTEQ; }
{LTEQ}		{ return yy::ScriptParser::token::LTEQ; }
{EQ}		{ return yy::ScriptParser::token::EQ; }
{UNEQ}		{ return yy::ScriptParser::token::UNEQ; }
{QUEST}		{ return yy::ScriptParser::token::QUEST; }
{COLON}		{ return yy::ScriptParser::token::COLON; }
{LPAREN}	{ return yy::ScriptParser::token::LPAREN; }
{RPAREN}	{ return yy::ScriptParser::token::RPAREN; }

%%

int ScriptDriver::parse(const char *str, size_t l)
{
	yylex_init(&m_scanner);
	yy_scan_bytes((char *)str,l,m_scanner);
	yyset_extra(this,m_scanner);
	yy::ScriptParser parser(*this);
//	yyset_debug(1,m_scanner);
//	parser.set_debug_level(1);
	int r = parser.parse();
	yylex_destroy(m_scanner);
	return r;
}

extern "C"
int yywrap(yyscan_t scanner)
{
	return 1;
}


int yylex(yy::ScriptParser::semantic_type *yylval_param, ScriptDriver &drv)
{
	return drv.lex(yylval_param);
}


int ScriptDriver::lex(yy::ScriptParser::semantic_type *yylval_param)
{
	int r = yylex(yylval_param,m_scanner);
//	cout << "token " << r << endl;
	return r;
}

void yy::ScriptParser::error(const string &e)
{
//	cerr << "error: " << e << endl;
	log_warn(TAG,"parser error %s",e.c_str());
	drv.setError();
}
