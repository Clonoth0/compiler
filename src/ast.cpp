#include<cassert>
#include<map>
#include<unordered_map>
#include"include/ast.hpp"

bool debug_flag=false;
koopa_stream out;
static int result_total=0,symbol_total=0;
static unordered_map<string,Symbol>symbol_table;
static vector<vector<pair<string,optional<Symbol>>>>mem;
static int if_total=0,ex_total=0; // basic block
static bool need_jump=false,need_ex=false;
static void symbol_insert(const string &ident,const Symbol &symbol)
{
	assert(!mem.empty());
	auto p=symbol_table.find(ident);
	if(p==symbol_table.end())
	{
		mem.back().emplace_back(ident,nullopt);
		symbol_table[ident]=symbol;
	}
	else
	{
		mem.back().emplace_back(ident,p->second);
		p->second=symbol;
	}
}
static void symbol_reset(const string &ident,const optional<Symbol>&symbol)
{
	auto p=symbol_table.find(ident);
	assert(p!=symbol_table.end());
	if(symbol.has_value())
		p->second=*symbol;
	else
		symbol_table.erase(p);
}
static Symbol _alloc()
{
	Symbol symbol(true,symbol_total++);
	out<<"\t"<<symbol<<" = alloc i32\n";
	need_jump=true;
	return symbol;
}
static Result _load(const Symbol &symbol)
{
	assert(symbol.addr);
	Result result(false,result_total++);
	out<<"\t"<<result<<" = load "<<symbol<<"\n";
	need_jump=true;
	return result;
}
static void _store(const Result &result,const Symbol &symbol)
{
	assert(symbol.addr);
	out<<"\t"<<"store "<<result<<", "<<symbol<<"\n";
}
static void add_basic_block(const string &str)
{
	if(need_jump)
		out<<"\tjump "<<str<<"\n";
	out<<str<<":\n";
	need_jump=true;
	need_ex=false;
}
static string basic_block_then(const int &x)
{
	return "%then_"+to_string(x);
}
static string basic_block_else(const int &x)
{
	return "%else_"+to_string(x);
}
static string basic_block_end(const int &x)
{
	return "%end_"+to_string(x);
}
static void solve_if_else(const node &exp,const node &stmt1,const node &stmt2)
{
	int x=if_total++;
	auto e=exp->print();
	out<<"\tbr "<<e<<", "<<basic_block_then(x)<<", "<<basic_block_else(x)<<"\n";
	need_jump=false;
	add_basic_block(basic_block_then(x));
	stmt1->print();
	if(need_jump)
	{
		out<<"\tjump "<<basic_block_end(x)<<"\n";
		need_jump=false;
	}
	add_basic_block(basic_block_else(x));
	stmt2->print();
	add_basic_block(basic_block_end(x));
}
static void solve_if(const node &exp,const node &stmt)
{
	int x=if_total++;
	auto e=exp->print();
	out<<"\tbr "<<e<<", "<<basic_block_then(x)<<", "<<basic_block_end(x)<<"\n";
	need_jump=false;
	add_basic_block(basic_block_then(x));
	stmt->print();
	add_basic_block(basic_block_end(x));
}
static void check_ex()
{
	if(need_ex)
		add_basic_block("%ex_"+to_string(ex_total++));
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
	out<<"%entry:\n";
	auto x=block->print();
	out<<"}\n";
	return x;
}
Result BlockAST::print()const
{
	if(debug_flag)
		out<<"Block :\n";
	mem.push_back({});
	Result now;
	for(const auto &block:*blocks)
		now=block->print();
	for(const auto &[ident,symbol]:mem.back())
		symbol_reset(ident,symbol);
	mem.pop_back();
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
	stmt->print();
	return Result();
}
Result MatchedStmtAST::print()const
{
	if(debug_flag)
		out<<"MatchedStmt :\n";
	if(type==_IF)
	{
		solve_if_else(*exp,stmt1,stmt2);
		return Result();
	}
	if(lval.has_value())
	{
		assert(exp.has_value());
		auto now=symbol_table[*lval];
		auto x=(*exp)->print();
		_store(x,now);
		return x;
	}
	if(type==_RETURN)
	{
		check_ex();
		if(exp.has_value())
		{
			auto x=(*exp)->print();
			out<<"\tret "<<x<<"\n";
			need_jump=false;
			need_ex=true;
			return x;
		}
		else
		{
			out<<"\tret\n";
			need_jump=false;
			need_ex=true;
			return Result();
		}
	}
	if(block.has_value())
	{
		auto x=(*block)->print();
		return x;
	}
	if(exp.has_value())
	{
		auto x=(*exp)->print();
		return x;
	}
	return Result();
}

Result DanglingStmtAST::print()const
{
	if(debug_flag)
		out<<"DanglingStmt :\n";
	if(matched_stmt.has_value())
		solve_if_else(exp,*matched_stmt,stmt);
	else
		solve_if(exp,stmt);
	return Result();
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
	symbol_insert(ident,Symbol(false,now.value));
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
	symbol_insert(ident,now);
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
			check_ex();
			if(op.value()=="!")
				out<<"\t"<<now<<" = eq "<<x<<", 0\n";
			else
			{
				assert(op.value()=="-");
				out<<"\t"<<now<<" = sub 0, "<<x<<"\n";
			}
			need_jump=true;
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
			check_ex();
			if(value->second=="*")
				out<<"\t"<<now<<" = mul "<<x<<", "<<y<<"\n";
			if(value->second=="/")
				out<<"\t"<<now<<" = div "<<x<<", "<<y<<"\n";
			if(value->second=="%")
				out<<"\t"<<now<<" = mod "<<x<<", "<<y<<"\n";
			need_jump=true;
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
			check_ex();
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
			check_ex();
			if(value->second=="<")
				out<<"\t"<<now<<" = lt "<<x<<", "<<y<<"\n";
			if(value->second==">")
				out<<"\t"<<now<<" = gt "<<x<<", "<<y<<"\n";
			if(value->second=="<=")
				out<<"\t"<<now<<" = le "<<x<<", "<<y<<"\n";
			if(value->second==">=")
				out<<"\t"<<now<<" = gt "<<x<<", "<<y<<"\n";
			need_jump=true;
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
			check_ex();
			if(value->second=="==")
				out<<"\t"<<now<<" = eq "<<x<<", "<<y<<"\n";
			if(value->second=="!=")
				out<<"\t"<<now<<" = ne "<<x<<", "<<y<<"\n";
			need_jump=true;
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
		if(x.imm)
		{
			if(!x.value)
				return Result(true,0);
			else
			{
				auto y=eq_exp->print();
				return y;
			}
		}
		else
		{
			auto p=_alloc();
			int i=if_total++;
			check_ex();
			out<<"\tbr "<<x<<", "<<basic_block_then(i)<<", "<<basic_block_else(i)<<"\n";
			need_jump=false;
			add_basic_block(basic_block_then(i));
			auto y=eq_exp->print();
			if(y.imm)
				_store(Result(true,(bool)y.value),p);
			else
			{
				Result v(false,result_total++);
				out<<"\t"<<v<<" = ne "<<y<<", 0\n";
				_store(v,p);
			}
			out<<"\tjump "<<basic_block_end(i)<<"\n";
			need_jump=false;
			add_basic_block(basic_block_else(i));
			_store(Result(true,0),p);
			add_basic_block(basic_block_end(i));
			return _load(p);
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
		if(x.imm)
		{
			if(x.value)
				return Result(true,1);
			else
			{
				auto y=and_exp->print();
				return y;
			}
		}
		else
		{
			auto p=_alloc();
			int i=if_total++;
			check_ex();
			out<<"\tbr "<<x<<", "<<basic_block_then(i)<<", "<<basic_block_else(i)<<"\n";
			need_jump=false;
			add_basic_block(basic_block_then(i));
			_store(Result(true,1),p);
			out<<"\tjump "<<basic_block_end(i)<<"\n";
			need_jump=false;
			add_basic_block(basic_block_else(i));
			auto y=and_exp->print();
			if(y.imm)
				_store(Result(true,(bool)y.value),p);
			else
			{
				Result v(false,result_total++);
				out<<"\t"<<v<<" = ne "<<y<<", 0\n";
				_store(v,p);
			}
			add_basic_block(basic_block_end(i));
			return _load(p);
		}
	}
}