#include "parser.h"

EnvNumber *EnvNumber::First = 0;

const char Script [] = 
	"x := 1+3\n"
	"a := x*2\n"
	"d := $sin(30)\n"
	"e := $sqrt(1000)\n"
	"x := a-d/e\n"
;

int main()
{
	ScriptDriver parser;

	parser.parse(Script,sizeof(Script));

	return 0;

}
