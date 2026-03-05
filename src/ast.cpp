#include<cassert>
#include"include/ast.hpp"

bool debug_flag=false;
koopa_stream out;
static int total=0;
Result CompUnitAST::print()const
{
	if(debug_flag)
		out<<"CompUnit :\n";
	auto x=func_def->print();
	return x;
}
Result FuncDefAST::print()const
{
	if(debug_flag)
		out<<"FuncDef :\n";
	out<<"fun @"<<ident<<"(): "<<type<<" {\n";
	auto x=block->print();
	out<<"}\n";
	return x;
}
Result BlockAST::print()const
{
	if(debug_flag)
		out<<"Block :\n";
	out<<"%entry:\n";
	auto x=stmt->print();
	return x;
}
Result StmtAST::print()const
{
	if(debug_flag)
		out<<"Stmt :\n";
	auto x=exp->print();
	out<<"\tret "<<x<<"\n";
	return x;
}
Result ExpAST::print()const
{
	if(debug_flag)
		out<<"Exp :\n";
	auto x=exp->print();
	return x;
}
Result UnaryExpAST::print()const
{
	if(debug_flag)
		out<<"UnaryExp :\n";
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
			Result now(false,total++);
			if(op.value()=="!")
				out<<"\t"<<now<<" = eq "<<x<<", 0\n";
			else
			{
				assert(op.value()=="-");
				out<<"\t"<<now<<" = sub 0, "<<x<<"\n";
			}
			return now;
		}
	}
}
Result PrimaryExpAST::print()const
{
	if(debug_flag)
		out<<"PrimaryExp :\n";
	if(number)
		return Result(true,*number);
	else
		return exp->print();
}
Result MulExpAST::print()const
{
	if(debug_flag)
		out<<"MulExpAST :\n";
	if(!value.has_value())
		return unary_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=unary_exp->print();
		Result now(false,total++);
		if(value->second=="*")
			out<<"\t"<<now<<" = mul "<<x<<", "<<y<<"\n";
		if(value->second=="/")
			out<<"\t"<<now<<" = div "<<x<<", "<<y<<"\n";
		if(value->second=="%")
			out<<"\t"<<now<<" = mod "<<x<<", "<<y<<"\n";
		return Result(now);
	}
}
Result AddExpAST::print()const
{
	if(debug_flag)
		out<<"AddExpAST :\n";
	if(!value.has_value())
		return mul_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=mul_exp->print();
		Result now(false,total++);
		if(value->second=="+")
			out<<"\t"<<now<<" = add "<<x<<", "<<y<<"\n";
		if(value->second=="-")
			out<<"\t"<<now<<" = sub "<<x<<", "<<y<<"\n";
		return now;
	}
}
Result RelExpAST::print()const
{
	if(debug_flag)
		out<<"RelExpAST :\n";
	if(!value.has_value())
		return add_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=add_exp->print();
		Result now(false,total++);
		if(value->second=="<")
			out<<"\t"<<now<<" = lt "<<x<<", "<<y<<"\n";
		if(value->second==">")
			out<<"\t"<<now<<" = gt "<<x<<", "<<y<<"\n";
		if(value->second=="<=")
			out<<"\t"<<now<<" = le "<<x<<", "<<y<<"\n";
		if(value->second==">=")
			out<<"\t"<<now<<" = gt "<<x<<", "<<y<<"\n";
		return now;
	}
}
Result EqExpAST::print()const
{
	if(debug_flag)
		out<<"EqExpAST :\n";
	if(!value.has_value())
		return rel_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=rel_exp->print();
		Result now(false,total++);
		if(value->second=="==")
			out<<"\t"<<now<<" = eq "<<x<<", "<<y<<"\n";
		if(value->second=="!=")
			out<<"\t"<<now<<" = ne "<<x<<", "<<y<<"\n";
		return now;
	}
}
Result AndExpAST::print()const
{
	if(debug_flag)
		out<<"AndExpAST :\n";
	if(!value.has_value())
		return eq_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=eq_exp->print();
		Result u(false,total++);
		out<<"\t"<<u<<" = ne "<<x<<", 0\n";
		Result v(false,total++);
		out<<"\t"<<v<<" = ne "<<y<<", 0\n";
		Result now(false,total++);
		out<<"\t"<<now<<" = and "<<u<<", "<<v<<"\n";
		return now;
	}
}
Result OrExpAST::print()const
{
	if(debug_flag)
		out<<"OrExpAST :\n";
	if(!value.has_value())
		return and_exp->print();
	else
	{
		auto x=value->first->print();
		auto y=and_exp->print();
		Result u(false,total++);
		out<<"\t"<<u<<" = ne "<<x<<", 0\n";
		Result v(false,total++);
		out<<"\t"<<v<<" = ne "<<y<<", 0\n";
		Result now(false,total++);
		out<<"\t"<<now<<" = or "<<u<<", "<<v<<"\n";
		return now;
	}
}