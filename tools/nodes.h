


struct Expression
{

};


struct BinaryExpression : public Expression
{

	typedef enum binop_e {
		binop_plus,
		binop_minus,
		binop_mult,
		binop_div,
		binop_mod,
		binop_power,
		binop_less,
		binop_lesseq,
		binop_greater,
		binop_greatereq,
		binop_equal,
		binop_unequal,
	} binop_t;
	Expression *m_left, *m_right;
	binop_t m_op;
};


struct Addition : public BinaryExpression
{

};


struct Function1Arg : public Expression
{
	std::string m_name;
	Expression *m_arg;
};


struct Function1Arg : public Expression
{
	std::string m_name;
	Expression *m_arg1;
	Expression *m_arg2;
};
