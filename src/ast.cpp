#include<cassert>
#include<map>
#include<unordered_map>
#include<unordered_set>
#include"include/ast.hpp"

bool debug_flag=false;
koopa_stream out;
static int result_total=0,symbol_total=0;
static unordered_map<string,Symbol>symbol_table;
static unordered_map<string,bool>func_table={{"main",true},{"getint",true},{"getch",true},{"getarray",true},{"putint",false},{"putch",false},{"putarray",false},{"starttime",false},{"stoptime",false}}; // int : true, void : false
static vector<vector<pair<string,optional<Symbol>>>>symbol_stack;
static unordered_set<string>builtin_funcs={"main","getint","getch","getarray","putint","putch","putarray","starttime","stoptime"};
static int if_total=0,while_total=0,ex_total=0; // basic block
static bool need_jump=false,need_ex=false;
static vector<int>while_stack;
static bool first_block,is_global;
static void symbol_insert(const string &ident,const Symbol &symbol)
{
	assert(!symbol_stack.empty());
	auto p=symbol_table.find(ident);
	if(p==symbol_table.end())
	{
		symbol_stack.back().emplace_back(ident,nullopt);
		symbol_table[ident]=symbol;
	}
	else
	{
		symbol_stack.back().emplace_back(ident,p->second);
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
static void add_basic_block(const string &str)
{
	if(need_jump)
		out<<"\tjump "<<str<<"\n";
	out<<str<<":\n";
	need_jump=true;
	need_ex=false;
}
static void check_ex()
{
	if(need_ex)
		add_basic_block("%ex_"+to_string(ex_total++));
}
static Symbol _alloc()
{
	check_ex();
	Symbol symbol(true,symbol_total++);
	out<<"\t"<<symbol<<" = alloc i32\n";
	need_jump=true;
	return symbol;
}
static Result _load(const Symbol &symbol)
{
	assert(symbol.addr);
	check_ex();
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
static string _if_then(const int &x)
{
	return "%then_"+to_string(x);
}
static string _if_else(const int &x)
{
	return "%else_"+to_string(x);
}
static string _if_end(const int &x)
{
	return "%if_end_"+to_string(x);
}
static string _while_entry(const int &x)
{
	return "%entry_"+to_string(x);
}
static string _while_body(const int &x)
{
	return "%body_"+to_string(x);
}
static string _while_end(const int &x)
{
	return "%while_end_"+to_string(x);
}
static void solve_if_else(const node &exp,const node &stmt1,const node &stmt2)
{
	int i=if_total++;
	auto e=exp->print();
	out<<"\tbr "<<e<<", "<<_if_then(i)<<", "<<_if_else(i)<<"\n";
	need_jump=false;
	add_basic_block(_if_then(i));
	stmt1->print();
	if(need_jump)
	{
		out<<"\tjump "<<_if_end(i)<<"\n";
		need_jump=false;
	}
	add_basic_block(_if_else(i));
	stmt2->print();
	add_basic_block(_if_end(i));
}
static void solve_if(const node &exp,const node &stmt)
{
	int i=if_total++;
	auto e=exp->print();
	out<<"\tbr "<<e<<", "<<_if_then(i)<<", "<<_if_end(i)<<"\n";
	need_jump=false;
	add_basic_block(_if_then(i));
	stmt->print();
	add_basic_block(_if_end(i));
}
static void solve_while(const node &exp,const node &stmt)
{
	int i=while_total++;
	while_stack.push_back(i);
	add_basic_block(_while_entry(i));
	auto e=exp->print();
	out<<"\tbr "<<e<<", "<<_while_body(i)<<", "<<_while_end(i)<<"\n";
	need_jump=false;
	add_basic_block(_while_body(i));
	stmt->print();
	if(need_jump)
	{
		out<<"\tjump "<<_while_entry(i)<<"\n";
		need_jump=false;
	}
	add_basic_block(_while_end(i));
	while_stack.pop_back();
}
Result ProgramAST::print()const
{
	if(debug_flag)
		out<<"Program :\n";
	symbol_stack.push_back({});
	for(const auto &def:*defs)
	{
		is_global=true;
		def->print();
	}
	return Result();
}
static vector<Symbol>def_params;
Result FuncDefAST::print()const
{
	if(debug_flag)
		out<<"FuncDef :\n";
	is_global=false;
	if(type.empty())
		func_table[ident]=false;
	else
		func_table[ident]=true;
	symbol_stack.push_back({});
	string str;
	if(builtin_funcs.count(ident))
		str="@"+ident;
	else
	{
		Symbol now(true,symbol_total++);
		symbol_table[ident]=now;
		str=now;
	}
	out<<"fun "<<str<<"(";
	bool first=true;
	def_params.clear();
	for(const auto &param:*params)
	{
		if(!first)
			out<<", ";
		param->print();
		first=false;
	}
	out<<")"<<type<<" {\n";
	str.erase(str.begin());
	add_basic_block("%entry_"+str);
	first_block=true;
	block->print();
	if(need_jump)
	{
		out<<"\tret\n";
		need_jump=false;
	}
	out<<"}\n";
	return Result();
}
Result FuncFParamAST::print()const
{
	if(debug_flag)
		out<<"FuncFParam :\n";
	Symbol now(true,symbol_total++);
	def_params.push_back(now);
	out<<now<<"_: i32";
	symbol_insert(ident,now);
	return Result();
}
Result BlockAST::print()const
{
	if(debug_flag)
		out<<"Block :\n";
	if(!first_block)
		symbol_stack.push_back({});
	else
	{
		for(const auto &now:def_params)
		{
			out<<"\t"<<now<<" = alloc i32\n";
			out<<"\tstore "<<now<<"_, "<<now<<"\n";
		}
		first_block=false;
	}
	Result now;
	for(const auto &block:*blocks)
		now=block->print();
	for(const auto &[ident,symbol]:symbol_stack.back())
		symbol_reset(ident,symbol);
	symbol_stack.pop_back();
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
	if(type==_IF_ELSE)
	{
		assert(exp.has_value());
		solve_if_else(*exp,stmt1,stmt2);
		return Result();
	}
	if(type==_WHILE)
	{
		assert(exp.has_value());
		solve_while(*exp,stmt1);	
		return Result();
	}
	if(type==_BREAK)
	{
		int i=while_stack.back();
		out<<"\tjump "<<_while_end(i)<<"\n";
		need_jump=false;
		need_ex=true;
		return Result();
	}
	if(type==_CONTINUE)
	{
		int i=while_stack.back();
		out<<"\tjump "<<_while_entry(i)<<"\n";
		need_jump=false;
		need_ex=true;
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
	if(type==_IF)
		solve_if(exp,stmt1);
	if(type==_IF_ELSE)
		solve_if_else(exp,stmt1,stmt2);
	if(type==_WHILE)
		solve_while(exp,stmt1);	
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
	if(is_global)
	{
		Symbol now(true,symbol_total++);
		Result value(true,0);
		if(init.has_value())
			value=(*init)->print();
		assert(value.imm);
		symbol_insert(ident,now);
		out<<"global "<<now<<" = alloc i32, "<<value<<"\n";
		return value;
	}
	else
	{
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
	if(func)
	{
		assert(op.has_value());
		string str;
		if(builtin_funcs.count(*op))
			str="@"+*op;
		else
			str=symbol_table[*op];
		int size=params->size();
		vector<Result>result(size);
		for(int i=0;i<size;++i)
			result[i]=(*params)[i]->print();
		if(func_table[*op])
		{
			Result now(false,result_total++);
			out<<"\t"<<now<<" = call "<<str<<"(";
			for(int i=0;i<size;++i)
			{
				if(i)
					out<<", ";
				out<<result[i];
			}
			out<<")\n";
			return now;
		}
		else
		{
			out<<"\tcall "<<str<<"(";
			for(int i=0;i<size;++i)
			{
				if(i)
					out<<", ";
				out<<result[i];
			}
			out<<")\n";
			return Result();
		}
	}
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
				Result v(false,result_total++);
				out<<"\t"<<v<<" = ne "<<y<<", 0\n";
				return v;
			}
		}
		else
		{
			auto p=_alloc();
			int i=if_total++;
			check_ex();
			out<<"\tbr "<<x<<", "<<_if_then(i)<<", "<<_if_else(i)<<"\n";
			need_jump=false;
			add_basic_block(_if_then(i));
			auto y=eq_exp->print();
			if(y.imm)
				_store(Result(true,(bool)y.value),p);
			else
			{
				Result v(false,result_total++);
				out<<"\t"<<v<<" = ne "<<y<<", 0\n";
				_store(v,p);
			}
			out<<"\tjump "<<_if_end(i)<<"\n";
			need_jump=false;
			add_basic_block(_if_else(i));
			_store(Result(true,0),p);
			add_basic_block(_if_end(i));
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
				Result v(false,result_total++);
				out<<"\t"<<v<<" = ne "<<y<<", 0\n";
				return v;
			}
		}
		else
		{
			auto p=_alloc();
			int i=if_total++;
			check_ex();
			out<<"\tbr "<<x<<", "<<_if_then(i)<<", "<<_if_else(i)<<"\n";
			need_jump=false;
			add_basic_block(_if_then(i));
			_store(Result(true,1),p);
			out<<"\tjump "<<_if_end(i)<<"\n";
			need_jump=false;
			add_basic_block(_if_else(i));
			auto y=and_exp->print();
			if(y.imm)
				_store(Result(true,(bool)y.value),p);
			else
			{
				Result v(false,result_total++);
				out<<"\t"<<v<<" = ne "<<y<<", 0\n";
				_store(v,p);
			}
			add_basic_block(_if_end(i));
			return _load(p);
		}
	}
}