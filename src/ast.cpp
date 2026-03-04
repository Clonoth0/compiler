#include<cassert>
#include"include/ast.hpp"

extern bool debug_flag;
extern string out;
static int total=0;
Result CompUnitAST::print()const
{
	if(debug_flag)
		out+="CompUnit :\n";
	auto x=func_def->print();
	return x;
}
Result FuncDefAST::print()const
{
	if(debug_flag)
		out+="FuncDef :\n";
	out+="fun @"+ident+"(): "+type+" {\n";
	auto x=block->print();
	out+="}\n";
	return x;
}
Result BlockAST::print()const
{
	if(debug_flag)
		out+="Block :\n";
	out+="%entry:\n";
	auto x=stmt->print();
	return x;
}
Result StmtAST::print()const
{
	if(debug_flag)
		out+="Stmt :\n";
	auto x=exp->print();
	out+="\tret "+x.value+"\n";
	return x;
}
Result ExpAST::print()const
{
	if(debug_flag)
		out+="Exp :\n";
	auto x=unary_exp->print();
	return x;
}
Result UnaryExpAST::print()const
{
	if(debug_flag)
		out+="UnaryExp :\n";
	if(!op.has_value())
	{
		auto x=exp->print();
		return x;
	}
	else
	{
		auto x=exp->print();
		if(op.value()=="+")
			return x;
		else
		{
			Result now("%"+to_string(total++));
			if(op.value()=="!")
				out+="\t"+now.value+" = eq "+x.value+", 0\n";
			else
			{
				assert(op.value()=="-");
				out+="\t"+now.value+" = sub 0, "+x.value+"\n";
			}
			return now;
		}
	}
}
Result PrimaryExpAST::print()const
{
	if(debug_flag)
		out+="PrimaryExp :\n";
	if(number)
		return Result(to_string(*number));
	else
		return exp->print();
}