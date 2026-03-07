#include<cassert>
#include<map>
#include<unordered_map>
#include"include/ast.hpp"

bool debug_flag=false;
koopa_stream out;
static int result_total=0,symbol_total=0;
static unordered_map<string,Symbol>symbol_table;

static Symbol _alloc()
{
	Symbol symbol(true,symbol_total++);
	out<<"\t"<<symbol<<" = alloc i32\n";
	return symbol;
}
static Result _load(const Symbol &symbol)
{
	assert(symbol.addr);
	Result result(false,result_total++);
	out<<"\t"<<result<<" = load "<<symbol<<"\n";
	return result;
}
static void _store(const Result &result,const Symbol &symbol)
{
	assert(symbol.addr);
	out<<"\t"<<"store "<<result<<", "<<symbol<<"\n";
}

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
	Result now;
	for(const auto &block:*blocks)
		now=block->print();
	return now;
}
Result BlockItemAST::print()const
{
	if(debug_flag)
		out<<"BlockItem :\n";
	return block->print();
}
Result StmtAST::print()const
{
	if(debug_flag)
		out<<"Stmt :\n";
	if(lval.has_value())
	{
		auto now=symbol_table[*lval];
		auto x=exp->print();
		_store(x,now);
		return x;
	}
	else
	{
		auto x=exp->print();
		out<<"\tret "<<x<<"\n";
		return x;
	}
}




Result DeclAST::print()const
{
	if(debug_flag)
		out<<"Decl :\n";
	auto now=decl->print();
	return now;
}
Result ConstDeclAST::print()const
{
	if(debug_flag)
		out<<"ConstDecl :\n";
	Result now;
	for(const auto &def:*defs)
		now=def->print();
	return now;
}
Result ConstDefAST::print()const
{
	if(debug_flag)
		out<<"ConstDef :\n";
	auto now=init->print();
	symbol_table[ident]=Symbol(false,now.value);
	return now;
}
Result ConstInitValAST::print()const
{
	if(debug_flag)
		out<<"ConstInitVal :\n";
	auto now=exp->print();
	return now;
}
Result ConstExpAST::print()const
{
	if(debug_flag)
		out<<"ConstExp :\n";
	auto now=exp->print();
	return now;
}


Result VarDeclAST::print()const
{
	if(debug_flag)
		out<<"VarDeclAST :\n";
	Result now;
	for(const auto &def:*defs)
		now=def->print();
	return now;
}
Result VarDefAST::print()const
{
	if(debug_flag)
		out<<"VarDef :\n";
	auto now=_alloc();
	Result value;
	if(init.has_value())
	{
		value=(*init)->print();
		_store(value,now);
	}
	symbol_table[ident]=now;
	return value;
}
Result InitValAST::print()const
{
	if(debug_flag)
		out<<"InitVal :\n";
	auto now=exp->print();
	return now;
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
			Result now(false,result_total++);
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
	if(number.has_value())
		return Result(true,*number);
	else
		if(lval.has_value())
		{
			auto now=symbol_table[*lval];
			if(now.addr)
				return _load(now); 
			else
				return Result(true,now.value);
		}
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
		if(x.imm&&y.imm)
		{
			Result now(true);
			if(value->second=="*")
				now.value=x.value*y.value;
			if(value->second=="/")
				now.value=x.value/y.value;
			if(value->second=="%")
				now.value=x.value%y.value;
			return now;
		}
		else
		{
			Result now(false,result_total++);
			if(value->second=="*")
				out<<"\t"<<now<<" = mul "<<x<<", "<<y<<"\n";
			if(value->second=="/")
				out<<"\t"<<now<<" = div "<<x<<", "<<y<<"\n";
			if(value->second=="%")
				out<<"\t"<<now<<" = mod "<<x<<", "<<y<<"\n";
			return Result(now);
		}
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
		if(x.imm&&y.imm)
		{
			Result now(true);
			if(value->second=="+")
				now.value=x.value+y.value;
			if(value->second=="-")
				now.value=x.value-y.value;
			return now;
		}
		else
		{
			Result now(false,result_total++);
			if(value->second=="+")
				out<<"\t"<<now<<" = add "<<x<<", "<<y<<"\n";
			if(value->second=="-")
				out<<"\t"<<now<<" = sub "<<x<<", "<<y<<"\n";
			return now;
		}
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
		if(x.imm&&y.imm)
		{
			Result now(true);
			if(value->second=="<")
				now.value=(x.value<y.value);
			if(value->second==">")
				now.value=(x.value>y.value);
			if(value->second=="<=")
				now.value=(x.value<=y.value);
			if(value->second==">=")
				now.value=(x.value>=y.value);
			return now;
		}
		else
		{
			Result now(false,result_total++);
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
		if(x.imm&&y.imm)
		{
			Result now(true);
			if(value->second=="==")
				now.value=(x.value==y.value);
			if(value->second=="!=")
				now.value=(x.value!=y.value);
			return now;
		}
		else
		{
			Result now(false,result_total++);
			if(value->second=="==")
				out<<"\t"<<now<<" = eq "<<x<<", "<<y<<"\n";
			if(value->second=="!=")
				out<<"\t"<<now<<" = ne "<<x<<", "<<y<<"\n";
			return now;
		}
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
		if(x.imm&&y.imm)
		{
			Result now(true);
			now.value=(x.value&&y.value);
			return now;
		}
		else
		{
			Result u(false,result_total++);
			out<<"\t"<<u<<" = ne "<<x<<", 0\n";
			Result v(false,result_total++);
			out<<"\t"<<v<<" = ne "<<y<<", 0\n";
			Result now(false,result_total++);
			out<<"\t"<<now<<" = and "<<u<<", "<<v<<"\n";
			return now;
		}
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
		if(x.imm&&y.imm)
		{
			Result now(true);
			now.value=(x.value||y.value);
			return now;
		}
		else
		{
			Result u(false,result_total++);
			out<<"\t"<<u<<" = ne "<<x<<", 0\n";
			Result v(false,result_total++);
			out<<"\t"<<v<<" = ne "<<y<<", 0\n";
			Result now(false,result_total++);
			out<<"\t"<<now<<" = or "<<u<<", "<<v<<"\n";
			return now;
		}
	}
}