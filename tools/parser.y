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

%{
#define YYDEBUG 1
//#include "env.h"

using namespace std;

%}

%define api.parser.class {ScriptParser}

%code requires
{
	class ScriptDriver;
	class EnvNumber;
}

%param { ScriptDriver &drv }

%code {
#include "parser.h"
//#include "env.h"
}

%start Program_P

%union {
	float f;
	EnvNumber *var;
	float (*fn1arg)(float);
	float (*fn2arg)(float,float);
}

%token <var> IDENT
%token <fn1arg> FUNC1ARG
%token <fn2arg> FUNC2ARG
%token <f> FLOAT
%token PLUS MINUS MUL DIV MOD LPAREN RPAREN COMMA NOT LT
	LTEQ GT GTEQ EQ UNEQ AND OR POWER
	QUEST COLON ASSIGN NL

%type <f> PrimaryExpression_P Expression_P PostfixExpression_P MultiplicativeExpression_P AdditiveExpression_P RelationalExpression_P EqualityExpression_P AndExpression_P OrExpression_P ConditionalExpression_P


%left PLUS MINUS MUL DIV MOD OR AND NOT 
%right ASSIGN

%%

PrimaryExpression_P
	: FLOAT
	{ $$ = new Value($1); }
	| MINUS FLOAT
	{ $$ = new Value(-$2); }
	| IDENT
	{ $$ = new LValue($1); }
	| LPAREN Expression_P RPAREN
	{ $$ = $2; }
	;

PostfixExpression_P
	: PrimaryExpression_P
	{ $$ = $1; }
	| FUNC1ARG LPAREN AdditiveExpression_P RPAREN
	{ $$ = new Func1Arg($1,$3); }
	| FUNC2ARG LPAREN AdditiveExpression_P COMMA AdditiveExpression_P RPAREN
	{ $$ = new Func2Arg($1,$3,$5); }
	;

MultiplicativeExpression_P
	: PostfixExpression_P
	{ $$ = $1; }
	| MultiplicativeExpression_P MUL PostfixExpression_P
	{ $$ = new Multiply($1,$3); }
	| MultiplicativeExpression_P DIV PostfixExpression_P
	{ $$ = new Divide($1,$3); }
	| MultiplicativeExpression_P MOD PostfixExpression_P
	{ $$ = new Modulo($1,$3); }
	;

AdditiveExpression_P
	: MultiplicativeExpression_P
	{ $$ = $1; }
	| AdditiveExpression_P PLUS MultiplicativeExpression_P
	{ $$ = new Addition($1,$3); }
	| AdditiveExpression_P MINUS MultiplicativeExpression_P
	{ $$ = new Substraction($1,$3); }
	;

RelationalExpression_P
	: AdditiveExpression_P
	{ $$ = $1; }
	| RelationalExpression_P LT AdditiveExpression_P
	{ $$ = new Less($1,$3); }
	| RelationalExpression_P GT AdditiveExpression_P
	{ $$ = new Greter($1,$3); }
	| RelationalExpression_P LTEQ AdditiveExpression_P
	{ $$ = new LessEq($1,$3); }
	| RelationalExpression_P GTEQ AdditiveExpression_P
	{ $$ = new GreaterEq($1,$3); }
	;

EqualityExpression_P
	: RelationalExpression_P
	{ $$ = $1; }
	| EqualityExpression_P EQ RelationalExpression_P
	{ $$ = new Equal($1,$3); }
	| EqualityExpression_P UNEQ RelationalExpression_P
	{ $$ = new Unequal($1,$3); }
	;

AndExpression_P
	: EqualityExpression_P
	{ $$ = $1; }
	| AndExpression_P AND EqualityExpression_P
	{ $$ = new And($1,$3); }
	;

OrExpression_P
	: AndExpression_P
	{ $$ = $1; }
	| OrExpression_P OR AndExpression_P
	{ $$ = new Or($1,$3); }
	;

ConditionalExpression_P
	: OrExpression_P
	{ $$ = $1; }
	| OrExpression_P QUEST Expression_P COLON ConditionalExpression_P
	{ $$ = new Conditonal($1,$3,$5); }
	;

Expression_P
	: ConditionalExpression_P
	{ $$ = $1; }

Assignment_P
	: IDENT ASSIGN ConditionalExpression_P NL
	{ $$ = new Assign($1,$3); }
	;

Program_P
	: Assignment_P
	{ }
	| Program_P Assignment_P
	{}
	;

%%

