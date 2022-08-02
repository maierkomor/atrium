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
	{ $$ = $1; }
	| MINUS FLOAT
	{ $$ = -$2; }
	| IDENT
	{ $$ = $1->get(); }
	| LPAREN Expression_P RPAREN
	{ $$ = $2; }
	;

PostfixExpression_P
	: PrimaryExpression_P
	{ $$ = $1; }
	| FUNC1ARG LPAREN AdditiveExpression_P RPAREN
	{ $$ = $1($3); }
	| FUNC2ARG LPAREN AdditiveExpression_P COMMA AdditiveExpression_P RPAREN
	{ $$ = $1($3,$5); }
	;

MultiplicativeExpression_P
	: PostfixExpression_P
	{ $$ = $1; }
	| MultiplicativeExpression_P MUL PostfixExpression_P
	{ $$ = $1 * $3; }
	| MultiplicativeExpression_P DIV PostfixExpression_P
	{ $$ = $1 / $3; }
	| MultiplicativeExpression_P MOD PostfixExpression_P
	{ $$ = fmodf($1,$3); }
	;

AdditiveExpression_P
	: MultiplicativeExpression_P
	{ $$ = $1; }
	| AdditiveExpression_P PLUS MultiplicativeExpression_P
	{ $$ = $1 + $3; }
	| AdditiveExpression_P MINUS MultiplicativeExpression_P
	{ $$ = $1 - $3; }
	;

RelationalExpression_P
	: AdditiveExpression_P
	{ $$ = $1; }
	| RelationalExpression_P LT AdditiveExpression_P
	{ $$ = $1 < $3; }
	| RelationalExpression_P GT AdditiveExpression_P
	{ $$ = $1 > $3; }
	| RelationalExpression_P LTEQ AdditiveExpression_P
	{ $$ = $1 <= $3; }
	| RelationalExpression_P GTEQ AdditiveExpression_P
	{ $$ = $1 >= $3; }
	;

EqualityExpression_P
	: RelationalExpression_P
	{ $$ = $1; }
	| EqualityExpression_P EQ RelationalExpression_P
	{ $$ = $1 == $3; }
	| EqualityExpression_P UNEQ RelationalExpression_P
	{ $$ = $1 != $3; }
	;

AndExpression_P
	: EqualityExpression_P
	{ $$ = $1; }
	| AndExpression_P AND EqualityExpression_P
	{ $$ = ($1 != 0) && ($3 != 0); }
	;

OrExpression_P
	: AndExpression_P
	{ $$ = $1; }
	| OrExpression_P OR AndExpression_P
	{ $$ = ($1 != 0) || ($3 != 0); }
	;

ConditionalExpression_P
	: OrExpression_P
	{ $$ = $1; }
	| OrExpression_P QUEST Expression_P COLON ConditionalExpression_P
	{ $$ = $1 ? $3 : $5; }
	;

Expression_P
	: ConditionalExpression_P
	{ $$ = $1; }

Assignment_P
	: IDENT ASSIGN ConditionalExpression_P NL
	{ $1->set($3); }
	;

Program_P
	: Assignment_P
	{ }
	| Program_P Assignment_P
	{}
	;

%%

