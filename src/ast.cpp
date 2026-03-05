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
	auto x=exp->print();
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
			string now("%"+to_string(total++));
			if(op.value()=="!")
				out+="\t"+now+" = eq "+x.value+", 0\n";
			else
			{
				assert(op.value()=="-");
				out+="\t"+now+" = sub 0, "+x.value+"\n";
			}
			return Result(now);
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
Result MulExpAST::print()const
{
	if(debug_flag)
		out+="MulExpAST :\n";
	if(!value.has_value())
		return unary_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=unary_exp->print();
		string now("%"+to_string(total++));
		if(value->second=="*")
			out+="\t"+now+" = mul "+x.value+", "+y.value+"\n";
		if(value->second=="/")
			out+="\t"+now+" = div "+x.value+", "+y.value+"\n";
		if(value->second=="%")
			out+="\t"+now+" = mod "+x.value+", "+y.value+"\n";
		return Result(now);
	}
}
Result AddExpAST::print()const
{
	if(debug_flag)
		out+="AddExpAST :\n";
	if(!value.has_value())
		return mul_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=mul_exp->print();
		string now("%"+to_string(total++));
		if(value->second=="+")
			out+="\t"+now+" = add "+x.value+", "+y.value+"\n";
		if(value->second=="-")
			out+="\t"+now+" = sub "+x.value+", "+y.value+"\n";
		return Result(now);
	}
}
Result RelExpAST::print()const
{
	if(debug_flag)
		out+="RelExpAST :\n";
	if(!value.has_value())
		return add_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=add_exp->print();
		string now("%"+to_string(total++));
		if(value->second=="<")
			out+="\t"+now+" = lt "+x.value+", "+y.value+"\n";
		if(value->second==">")
			out+="\t"+now+" = gt "+x.value+", "+y.value+"\n";
		if(value->second=="<=")
			out+="\t"+now+" = le "+x.value+", "+y.value+"\n";
		if(value->second==">=")
			out+="\t"+now+" = ge "+x.value+", "+y.value+"\n";
		return Result(now);
	}
}
Result EqExpAST::print()const
{
	if(debug_flag)
		out+="EqExpAST :\n";
	if(!value.has_value())
		return rel_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=rel_exp->print();
		string now("%"+to_string(total++));
		if(value->second=="==")
			out+="\t"+now+" = eq "+x.value+", "+y.value+"\n";
		if(value->second=="!=")
			out+="\t"+now+" = ne "+x.value+", "+y.value+"\n";
		return Result(now);
	}
}
Result AndExpAST::print()const
{
	if(debug_flag)
		out+="AndExpAST :\n";
	if(!value.has_value())
		return eq_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=eq_exp->print();
		string u("%"+to_string(total++));
		out+="\t"+u+" = ne "+x.value+", 0\n";
		string v("%"+to_string(total++));
		out+="\t"+v+" = ne "+y.value+", 0\n";
		string now("%"+to_string(total++));
		out+="\t"+now+" = and "+u+", "+v+"\n";
		return Result(now);
	}
}
Result OrExpAST::print()const
{
	if(debug_flag)
		out+="OrExpAST :\n";
	if(!value.has_value())
		return and_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=and_exp->print();
		string u("%"+to_string(total++));
		out+="\t"+u+" = ne "+x.value+", 0\n";
		string v("%"+to_string(total++));
		out+="\t"+v+" = ne "+y.value+", 0\n";
		string now("%"+to_string(total++));
		out+="\t"+now+" = or "+u+", "+v+"\n";
		return Result(now);
	}
}