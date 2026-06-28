#include<cassert>
#include<map>
#include<set>
#include<unordered_map>
#include<unordered_set>
#include"include/ast.hpp"
Result::Result(const Symbol &rhs)
{
	imm=!rhs.addr,value=rhs.value;
}
Symbol::Symbol(const Result &rhs)
{
	addr=!rhs.imm,value=rhs.value;
}
bool debug_flag=false;
koopa_stream out;
static int _total=0;
static unordered_map<string,Symbol>symbol_table;
static map<Symbol,vector<int>>symbol_size;
static set<Symbol>ptr_value;
static unordered_map<string,bool>func_table={{"main",true},{"getint",true},{"getch",true},{"getarray",true},{"putint",false},{"putch",false},{"putarray",false},{"starttime",false},{"stoptime",false}}; // int : true, void : false
static vector<vector<pair<string,optional<Symbol>>>>symbol_stack;
static unordered_set<string>builtin_funcs={"main","getint","getch","getarray","putint","putch","putarray","starttime","stoptime"};
struct FuncSig
{
	int param_count;
	bool returns_int;
};
static unordered_map<string,int>func_id;
static unordered_map<string,int>func_symbol_val;
static unordered_map<string,FuncSig>func_sigs;
static int next_func_id=0;
static int next_lambda_id=0;
static vector<const LambdaExpAST*>pending_lambdas;
static unordered_map<string,ClosureLayout>closure_layouts;
static unordered_set<string>self_params;
static string current_lambda_func_name;
static int next_thunk_id=0;
static bool in_auto_def=false;
struct ThunkInfo{string thunk_name;string real_lambda_name;vector<string>capture_names;int user_param_count;int cap_count;string ret_type;};
static vector<ThunkInfo>pending_thunks;
static int if_total=0,while_total=0,ex_total=0; // basic block
static bool need_jump=false,need_ex=false;
static vector<int>while_stack;
static bool first_block,is_global;
static vector<int>array_size;
static vector<Result>array_init;
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
	Symbol symbol(true,_total++);
	out<<"\t"<<symbol<<" = alloc i32\n";
	need_jump=true;
	return symbol;
}
static Symbol _alloc(const int &size)
{
	check_ex();
	Symbol symbol(true,_total++);
	out<<"\t"<<symbol<<" = alloc [i32, "<<size<<"]\n";
	need_jump=true;
	return symbol;
}
static Symbol _getelemptr(const Symbol &array,const Result &index)
{
	check_ex();
	Symbol symbol(true,_total++);
	out<<"\t"<<symbol<<" = getelemptr "<<array<<", "<<index<<"\n";
	need_jump=true;
	return symbol;
}
static Symbol _getptr(const Symbol &ptr,const Result &index)
{
	check_ex();
	Symbol symbol(true,_total++);
	out<<"\t"<<symbol<<" = getptr "<<ptr<<", "<<index<<"\n";
	need_jump=true;
	return symbol;
}
static Result _load(const Symbol &symbol)
{
	assert(symbol.addr);
	check_ex();
	Result result(false,_total++);
	out<<"\t"<<result<<" = load "<<symbol<<"\n";
	need_jump=true;
	return result;
}
static void _store(const Result &result,const Symbol &symbol)
{
	assert(symbol.addr);
	check_ex();
	out<<"\t"<<"store "<<result<<", "<<symbol<<"\n";
	need_jump=true;
}
static Result _add(const Result &lhs,const Result &rhs)
{
	check_ex();
	Result now(false,_total++);
	out<<"\t"<<now<<" = add "<<lhs<<", "<<rhs<<"\n";
	need_jump=true;
	return now;
}
static Result _mul(const Result &lhs,const Result &rhs)
{
	check_ex();
	Result now(false,_total++);
	out<<"\t"<<now<<" = mul "<<lhs<<", "<<rhs<<"\n";
	need_jump=true;
	return now;
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
	auto e=exp->print();
	if(e.imm)
	{
		if(e.value)
			stmt1->print();
		else
			stmt2->print();
		return;
	}
	int i=if_total++;
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
	auto e=exp->print();
	if(e.imm)
	{
		if(e.value)
			stmt->print();
		return;
	}
	int i=if_total++;
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
	if(e.imm)
	{
		if(!e.value)
		{
			out<<"\tjump "<<_while_end(i)<<"\n";
			need_jump=false;
			add_basic_block(_while_end(i));
			while_stack.pop_back();
			return;
		}
		stmt->print();
		if(need_jump)
		{
			out<<"\tjump "<<_while_entry(i)<<"\n";
			need_jump=false;
		}
		add_basic_block(_while_end(i));
		while_stack.pop_back();
		return;
	}
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
	for(const auto &def:*defs)
		def->pre_register();
	for(int n=0;n<=8;++n)
	{
		out<<"decl @__fp_"<<n<<"_i32(i32";
		for(int j=0;j<n;++j)
			out<<", i32";
		out<<"): i32\n";
		out<<"decl @__fp_"<<n<<"_void(i32";
		for(int j=0;j<n;++j)
			out<<", i32";
		out<<")\n";
	}
	symbol_stack.push_back({});
	auto emit=[&](bool fp)
	{
		for(const auto &def:*defs)
		{
			if(def->has_func_ptr_params()==fp)
			{
				auto fd=dynamic_cast<FuncDefAST*>(def.get());
				if(fd&&fd->ident=="main")
					continue;
				if(fd&&!builtin_funcs.count(fd->ident))
					func_id[fd->ident]=next_func_id++;
				is_global=true;
				def->print();
			}
		}
	};
	emit(false);
	emit(true);
	for(const auto &def:*defs)
	{
		auto fd=dynamic_cast<FuncDefAST*>(def.get());
		if(fd&&fd->ident=="main")
		{
			is_global=true;
			def->print();
		}
	}
	for(size_t i=0;i<pending_lambdas.size();++i)
		pending_lambdas[i]->print_body();
	return Result();
}
static vector<pair<Symbol,bool>>def_params; // ptr : true, value : false
void FuncDefAST::pre_register()const
{
	func_sigs[ident]={(int)params->size(),!type.empty()};
	if(!builtin_funcs.count(ident))
	{
		if(type.empty())
			func_table[ident]=false;
		else
			func_table[ident]=true;
		Symbol now(true,_total++);
		symbol_table[ident]=now;
		func_symbol_val[ident]=now.value;
	}
}
bool FuncDefAST::has_func_ptr_params()const
{
	for(const auto &param:*params)
		if(param->is_func_ptr_param())
			return true;
	return false;
}
static void walk_ast(const node &n,unordered_set<string>&found)
{
	if(!n) return;
	if(auto lv=dynamic_cast<LValAST*>(n.get())){found.insert(lv->ident);return;}
	if(auto p=dynamic_cast<PrimaryExpAST*>(n.get())){if(p->lval.has_value())walk_ast(*p->lval,found);else if(p->exp)walk_ast(p->exp,found);return;}
	if(auto u=dynamic_cast<UnaryExpAST*>(n.get())){if(u->func){if(u->params)for(auto &x:*u->params)walk_ast(x,found);}else if(u->exp)walk_ast(u->exp,found);return;}
	if(auto b=dynamic_cast<MulExpAST*>(n.get())){walk_ast(b->unary_exp,found);if(b->value.has_value())walk_ast(b->value->first,found);return;}
	if(auto a=dynamic_cast<AddExpAST*>(n.get())){walk_ast(a->mul_exp,found);if(a->value.has_value())walk_ast(a->value->first,found);return;}
	if(auto r=dynamic_cast<RelExpAST*>(n.get())){walk_ast(r->add_exp,found);if(r->value.has_value())walk_ast(r->value->first,found);return;}
	if(auto e=dynamic_cast<EqExpAST*>(n.get())){walk_ast(e->rel_exp,found);if(e->value.has_value())walk_ast(e->value->first,found);return;}
	if(auto nd=dynamic_cast<AndExpAST*>(n.get())){walk_ast(nd->eq_exp,found);if(nd->value.has_value())walk_ast(nd->value->first,found);return;}
	if(auto o=dynamic_cast<OrExpAST*>(n.get())){walk_ast(o->and_exp,found);if(o->value.has_value())walk_ast(o->value->first,found);return;}
	if(auto e=dynamic_cast<ExpAST*>(n.get())){walk_ast(e->exp,found);return;}
	if(auto s=dynamic_cast<MatchedStmtAST*>(n.get())){if(s->lval.has_value())walk_ast(*s->lval,found);if(s->exp.has_value())walk_ast(*s->exp,found);if(s->block.has_value())walk_ast(*s->block,found);if(s->stmt1)walk_ast(s->stmt1,found);if(s->stmt2)walk_ast(s->stmt2,found);return;}
	if(auto d=dynamic_cast<DanglingStmtAST*>(n.get())){walk_ast(d->exp,found);if(d->stmt1)walk_ast(d->stmt1,found);if(d->stmt2)walk_ast(d->stmt2,found);return;}
	if(auto s=dynamic_cast<StmtAST*>(n.get())){walk_ast(s->stmt,found);return;}
	if(auto b=dynamic_cast<BlockAST*>(n.get())){if(b->blocks)for(auto &x:*b->blocks)walk_ast(x,found);return;}
	if(auto bi=dynamic_cast<BlockItemAST*>(n.get())){walk_ast(bi->block,found);return;}
	if(auto vd=dynamic_cast<VarDefAST*>(n.get())){if(vd->init.has_value())walk_ast(*vd->init,found);return;}
	if(auto d=dynamic_cast<DeclAST*>(n.get())){walk_ast(d->decl,found);return;}
	if(auto v=dynamic_cast<VarDeclAST*>(n.get())){if(v->defs)for(auto &x:*v->defs)walk_ast(x,found);return;}
	if(auto i=dynamic_cast<InitValAST*>(n.get())){if(i->exp.has_value())walk_ast(*i->exp,found);if(i->inits)for(auto &x:*i->inits)walk_ast(x,found);return;}
	if(auto l=dynamic_cast<LambdaExpAST*>(n.get())){if(l->block)walk_ast(l->block,found);return;}
}
static void walk_decls(const node &n,unordered_set<string>&declared)
{
	if(!n) return;
	if(auto vd=dynamic_cast<VarDefAST*>(n.get())){declared.insert(vd->ident);}
	if(auto cd=dynamic_cast<ConstDefAST*>(n.get())){declared.insert(cd->ident);}
	if(auto b=dynamic_cast<BlockAST*>(n.get())){if(b->blocks)for(auto &x:*b->blocks)walk_decls(x,declared);return;}
	if(auto bi=dynamic_cast<BlockItemAST*>(n.get())){walk_decls(bi->block,declared);return;}
	if(auto d=dynamic_cast<DeclAST*>(n.get())){walk_decls(d->decl,declared);return;}
	if(auto v=dynamic_cast<VarDeclAST*>(n.get())){if(v->defs)for(auto &x:*v->defs)walk_decls(x,declared);return;}
	if(auto cd=dynamic_cast<ConstDeclAST*>(n.get())){if(cd->defs)for(auto &x:*cd->defs)walk_decls(x,declared);return;}
	if(auto s=dynamic_cast<StmtAST*>(n.get())){walk_decls(s->stmt,declared);return;}
	if(auto ms=dynamic_cast<MatchedStmtAST*>(n.get())){if(ms->lval.has_value())walk_decls(*ms->lval,declared);if(ms->exp.has_value())walk_decls(*ms->exp,declared);if(ms->block.has_value())walk_decls(*ms->block,declared);if(ms->stmt1)walk_decls(ms->stmt1,declared);if(ms->stmt2)walk_decls(ms->stmt2,declared);return;}
	if(auto ds=dynamic_cast<DanglingStmtAST*>(n.get())){if(ds->exp)walk_decls(ds->exp,declared);if(ds->stmt1)walk_decls(ds->stmt1,declared);if(ds->stmt2)walk_decls(ds->stmt2,declared);return;}
	if(auto e=dynamic_cast<ExpAST*>(n.get())){walk_decls(e->exp,declared);return;}
	if(auto o=dynamic_cast<OrExpAST*>(n.get())){walk_decls(o->and_exp,declared);if(o->value.has_value())walk_decls(o->value->first,declared);return;}
	if(auto a=dynamic_cast<AndExpAST*>(n.get())){walk_decls(a->eq_exp,declared);if(a->value.has_value())walk_decls(a->value->first,declared);return;}
	if(auto e=dynamic_cast<EqExpAST*>(n.get())){walk_decls(e->rel_exp,declared);if(e->value.has_value())walk_decls(e->value->first,declared);return;}
	if(auto r=dynamic_cast<RelExpAST*>(n.get())){walk_decls(r->add_exp,declared);if(r->value.has_value())walk_decls(r->value->first,declared);return;}
	if(auto a=dynamic_cast<AddExpAST*>(n.get())){walk_decls(a->mul_exp,declared);if(a->value.has_value())walk_decls(a->value->first,declared);return;}
	if(auto m=dynamic_cast<MulExpAST*>(n.get())){walk_decls(m->unary_exp,declared);if(m->value.has_value())walk_decls(m->value->first,declared);return;}
	if(auto u=dynamic_cast<UnaryExpAST*>(n.get())){if(!u->func)walk_decls(u->exp,declared);return;}
	if(auto p=dynamic_cast<PrimaryExpAST*>(n.get())){if(p->exp)walk_decls(p->exp,declared);return;}
	if(auto i=dynamic_cast<InitValAST*>(n.get())){if(i->exp.has_value())walk_decls(*i->exp,declared);if(i->inits)for(auto &x:*i->inits)walk_decls(x,declared);return;}
	if(auto l=dynamic_cast<LambdaExpAST*>(n.get())){if(l->block)walk_decls(l->block,declared);for(auto &p:*l->params){auto lp=dynamic_cast<LambdaParamAST*>(p.get());declared.insert(lp->ident);}return;}
}
void LambdaExpAST::collect_captures()const
{
	if(!(cap_ref||cap_val)) return;
	unordered_set<string>found;
	walk_ast(block,found);
	unordered_set<string>param_set;
	for(auto &p:*params){auto lp=dynamic_cast<LambdaParamAST*>(p.get());param_set.insert(lp->ident);}
	if(has_self)param_set.insert(self_name);
	unordered_set<string>declared;
	walk_decls(block,declared);
	captures.clear();
	for(auto &id:found){if(builtin_funcs.count(id))continue;if(func_symbol_val.count(id))continue;if(param_set.count(id))continue;if(declared.count(id))continue;captures.push_back(id);}
	unordered_set<string>seen;vector<string>uniq;for(auto &c:captures){if(!seen.count(c)){seen.insert(c);uniq.push_back(c);}}captures=uniq;
}
void LambdaExpAST::pre_register()const
{
	if(!lambda_name.empty())
		return;
	lambda_name="__lambda_"+to_string(next_lambda_id++);
	has_self=false;
	self_name="";
	user_count=0;
	for(const auto &p:*params){auto lp=dynamic_cast<LambdaParamAST*>(p.get());if(lp->is_self){has_self=true;self_name=lp->ident;}else ++user_count;}
	collect_captures();
	int total_params=captures.size()+user_count+(has_self?1:0);
	func_id[lambda_name]=next_func_id++;
	func_sigs[lambda_name]={total_params,!type.empty()};
	func_table[lambda_name]=!type.empty();
	if(!builtin_funcs.count(lambda_name)){Symbol now(true,_total++);symbol_table[lambda_name]=now;func_symbol_val[lambda_name]=now.value;}
	if(captures.size()>0){thunk_name="__thunk_"+to_string(next_thunk_id++);ThunkInfo info;info.thunk_name=thunk_name;info.real_lambda_name=lambda_name;info.capture_names=captures;info.user_param_count=user_count;info.cap_count=(int)captures.size();info.ret_type=type.empty()?"":": i32";pending_thunks.push_back(info);}
	pending_lambdas.push_back(this);
}
Result LambdaExpAST::print()const
{
	if(lambda_name.empty())
		pre_register();
	if(capture_count()==0||in_auto_def)
		return Result(true,func_id[lambda_name]);
	int K=capture_count();
	Result sp(false,_total++);
	out<<"\t"<<sp<<" = load @"<<thunk_name<<"_sp\n";
	need_jump=true;
	Result acc(false,_total++);
	if(K==1) acc=sp;
	else{out<<"\t"<<acc<<" = add 0, 0\n";for(int i=0;i<K;++i){Result tmp(false,_total++);out<<"\t"<<tmp<<" = add "<<acc<<", "<<sp<<"\n";acc=tmp;}}
	Result ns(false,_total++);
	out<<"\t"<<ns<<" = add "<<sp<<", 1\n";
	out<<"\tstore "<<ns<<", @"<<thunk_name<<"_sp\n";
	for(int i=0;i<K;++i)
	{
		Result offset;
		if(i==0) offset=acc;
		else{offset=Result(false,_total++);out<<"\t"<<offset<<" = add "<<acc<<", "<<i<<"\n";}
		Symbol slot(true,_total++);
		out<<"\t"<<slot<<" = getelemptr @"<<thunk_name<<"_stack, "<<offset<<"\n";
		ptr_value.insert(slot);
		Result val=_load(symbol_table[captures[i]]);
		_store(val,slot);
	}
	need_jump=true;
	return Result(true,func_id[thunk_name]);
}
void LambdaExpAST::print_body()const
{
	current_lambda_func_name=lambda_name;
	vector<Symbol>cap_param_syms(captures.size());
	out<<"fun "<<string(Symbol(true,func_symbol_val[lambda_name]))<<"(";
	bool first=true;
	Symbol self_param_sym;
	if(has_self){self_param_sym=Symbol(true,_total++);out<<self_param_sym<<"_: i32";first=false;}
	for(size_t i=0;i<captures.size();++i){if(!first)out<<", ";first=false;cap_param_syms[i]=Symbol(true,_total++);out<<cap_param_syms[i]<<"_: i32";}
	for(const auto &p:*params){auto lp=dynamic_cast<LambdaParamAST*>(p.get());if(lp->is_self)continue;if(!first)out<<", ";first=false;Symbol now(true,_total++);symbol_insert(lp->ident,now);out<<now<<"_: i32";}
	out<<")"<<type<<" {\n";
	string entry_label=string(Symbol(true,func_symbol_val[lambda_name]));
	entry_label.erase(entry_label.begin());
	symbol_stack.push_back({});
	add_basic_block("%entry_"+entry_label);
	first_block=true;
	if(has_self){Symbol self_sym(true,_total++);symbol_insert(self_name,self_sym);out<<"\t"<<self_sym<<" = alloc i32\n";out<<"\tstore "<<self_param_sym<<"_, "<<self_sym<<"\n";}
	ClosureLayout self_cl;self_cl.has_self=has_self;self_cl.user_param_count=user_count;self_cl.lambda_func_name=lambda_name;self_cl.has_captures=(captures.size()>0);closure_layouts[lambda_name]=self_cl;
	for(size_t i=0;i<captures.size();++i){Symbol cap_slot(true,_total++);symbol_insert(captures[i],cap_slot);out<<"\t"<<cap_slot<<" = alloc i32\n";out<<"\tstore "<<cap_param_syms[i]<<"_, "<<cap_slot<<"\n";closure_layouts[lambda_name].cap_slots.push_back(cap_slot);closure_layouts[lambda_name].capture_names.push_back(captures[i]);}
	for(const auto &p:*params)
	{
		auto lp=dynamic_cast<LambdaParamAST*>(p.get());
		if(lp->is_self)
			continue;
		auto it=symbol_table.find(lp->ident);
		assert(it!=symbol_table.end());
		Symbol now(true,_total++);
		out<<"\t"<<now<<" = alloc i32\n";
		out<<"\tstore "<<it->second<<"_, "<<now<<"\n";
		symbol_insert(lp->ident,now);
	}
	first_block=false;
	self_params.clear();
	if(has_self)
		self_params.insert(self_name);
	block->print();
	self_params.clear();
	if(need_jump)
	{
		if(type.empty())
			out<<"\tret\n";
		else
			out<<"\tret 0\n";
		need_jump=false;
	}
	out<<"}\n";
	for(const auto &[ident,symbol]:symbol_stack.back())
		symbol_reset(ident,symbol);
	symbol_stack.pop_back();
}
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
		str=symbol_table[ident];
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
		if(type.empty())
			out<<"\tret\n";
		else
			out<<"\tret 0\n";
		need_jump=false;
	}
	out<<"}\n";
	return Result();
}
Result FuncFParamAST::print()const
{
	if(debug_flag)
		out<<"FuncFParam :\n";
	Symbol now(true,_total++);
	symbol_insert(ident,now);
	if(ptr)
	{
		def_params.emplace_back(now,true);
		int size=exps->size();
		array_size.resize(size);
		for(int i=0;i<size;++i)
		{
			auto now=(*exps)[i]->print();
			assert(now.imm);
			array_size[i]=now.value;
		}
		array_size.insert(array_size.begin(),1);
		++size;
		for(int i=size-2;~i;--i)
			array_size[i]*=array_size[i+1];
		symbol_size[now]=array_size;
		ptr_value.insert(now);
		out<<now<<"_: *i32";
	}
	else if(func_ptr)
	{
		def_params.emplace_back(now,false);
		out<<now<<"_: i32";
	}
	else
	{
		def_params.emplace_back(now,false);
		out<<now<<"_: i32";
	}
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
		for(const auto &[now,flag]:def_params)
		{
			if(!flag)
				out<<"\t"<<now<<" = alloc i32\n";
			else
				out<<"\t"<<now<<" = alloc *i32\n";
			out<<"\tstore "<<now<<"_, "<<now<<"\n";
		}
		first_block=false;
	}
	Result now;
	for(const auto &block:*blocks)
		now=block->print();
	assert(symbol_stack.size());
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
		check_ex();
		out<<"\tjump "<<_while_end(i)<<"\n";
		need_jump=false;
		need_ex=true;
		return Result();
	}
	if(type==_CONTINUE)
	{
		int i=while_stack.back();
		check_ex();
		out<<"\tjump "<<_while_entry(i)<<"\n";
		need_jump=false;
		need_ex=true;
		return Result();
	}
	if(lval.has_value())
	{
		assert(exp.has_value());
		auto lv=dynamic_cast<LValAST*>((*lval).get());
		auto x=(*exp)->print();
		if(lv&&closure_layouts.count(lv->ident))
		{
			auto &lhs=closure_layouts[lv->ident];
			_store(x,lhs.func_slot);
			string rhs_id;
			auto find_lval=[&](auto&&self,const node&n)->void{
				if(!n||!rhs_id.empty())return;
				if(auto l=dynamic_cast<LValAST*>(n.get())){rhs_id=l->ident;return;}
				if(auto e=dynamic_cast<ExpAST*>(n.get())){self(self,e->exp);return;}
				if(auto l=dynamic_cast<OrExpAST*>(n.get())){self(self,l->and_exp);if(l->value.has_value())self(self,l->value->first);return;}
				if(auto l=dynamic_cast<AndExpAST*>(n.get())){self(self,l->eq_exp);if(l->value.has_value())self(self,l->value->first);return;}
				if(auto l=dynamic_cast<EqExpAST*>(n.get())){self(self,l->rel_exp);if(l->value.has_value())self(self,l->value->first);return;}
				if(auto l=dynamic_cast<RelExpAST*>(n.get())){self(self,l->add_exp);if(l->value.has_value())self(self,l->value->first);return;}
				if(auto l=dynamic_cast<AddExpAST*>(n.get())){self(self,l->mul_exp);if(l->value.has_value())self(self,l->value->first);return;}
				if(auto l=dynamic_cast<MulExpAST*>(n.get())){self(self,l->unary_exp);if(l->value.has_value())self(self,l->value->first);return;}
				if(auto u=dynamic_cast<UnaryExpAST*>(n.get())){if(!u->func)self(self,u->exp);return;}
				if(auto p=dynamic_cast<PrimaryExpAST*>(n.get())){if(p->lval.has_value())self(self,*p->lval);return;}
			};
			find_lval(find_lval,*exp);
			if(!rhs_id.empty()&&closure_layouts.count(rhs_id)){auto &rhs=closure_layouts[rhs_id];size_t N=rhs.cap_slots.size();if(rhs.cap_count>0)N=rhs.cap_count;for(size_t i=0;i<N;++i){Result val=_load(rhs.cap_slots[i]);_store(val,lhs.cap_slots[i]);}lhs.has_captures=(N>0);lhs.cap_count=N;}
			else for(auto &slot:lhs.cap_slots)_store(Result(true,0),slot);
			return x;
		}
		auto now=Symbol((*lval)->print());
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
	if(exps->empty())
	{
		auto now=init->print();
		symbol_insert(ident,Symbol(false,now.value));
		return Result();
	}
	else
	{
		int size=exps->size();
		array_size.resize(size);
		for(int i=0;i<size;++i)
		{
			auto now=(*exps)[i]->print();
			assert(now.imm);
			array_size[i]=now.value;
		}
		for(int i=size-2;~i;--i)
			array_size[i]*=array_size[i+1];
		array_init.clear();
		init->print();
		int len=array_init.size();
		assert(array_size[0]==len);
		if(is_global)
		{
			Symbol now(true,_total++);
			symbol_insert(ident,now);
			symbol_size[now]=array_size;
			out<<"global "<<now<<" = alloc [i32, "<<len<<"], {";
			for(int i=0;i<len;++i)
			{
				if(i)
					out<<", ";
				out<<array_init[i];
			}
			out<<"}\n";
		}
		else
		{
			auto now=_alloc(len);
			symbol_insert(ident,now);
			symbol_size[now]=array_size;
			for(int i=0;i<len;++i)
			{
				auto v=_getelemptr(now,Result(true,i));
				ptr_value.insert(v);
				_store(array_init[i],v);
			}
		}
		return Result();
	}
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
	if(is_auto)
	{
		auto lambda=dynamic_cast<LambdaExpAST*>((*init).get());
		Symbol now=_alloc();
		bool prev_auto_def=in_auto_def;
		in_auto_def=true;
		Result fid=(*init)->print();
		in_auto_def=prev_auto_def;
		_store(fid,now);
		int K=lambda->capture_count();
		vector<Symbol>cap_slots(K);
		for(int i=0;i<K;++i){cap_slots[i]=_alloc();string cap_name=lambda->captures[i];Symbol var_sym=symbol_table[cap_name];if(!var_sym.addr){auto cl=closure_layouts.find(current_lambda_func_name);if(cl!=closure_layouts.end()){auto &cn=cl->second.capture_names;auto &cs=cl->second.cap_slots;for(size_t ci=0;ci<cn.size();++ci)if(cn[ci]==cap_name&&ci<cs.size()){var_sym=cs[ci];break;}}}if(!var_sym.addr)continue;Result val=_load(var_sym);_store(val,cap_slots[i]);}
		symbol_insert(ident,now);
		ClosureLayout cl;cl.func_slot=now;cl.cap_slots=cap_slots;cl.capture_names=lambda->captures;cl.user_param_count=lambda->user_count;cl.has_self=lambda->has_self;cl.has_captures=(K>0);cl.cap_count=K;cl.lambda_func_name=lambda->lambda_name;closure_layouts[ident]=cl;
		return fid;
	}
	if(exps->empty())
	{
		if(is_global)
		{
			Symbol now(true,_total++);
			Result value(true,0);
			if(init.has_value())
				value=(*init)->print();
			assert(value.imm);
			symbol_insert(ident,now);
			if(value.value)
				out<<"global "<<now<<" = alloc i32, "<<value<<"\n";
			else
				out<<"global "<<now<<" = alloc i32, zeroinit\n";
			return value;
		}
		else
		{
			if(func_ptr)
			{
				Symbol func_slot=_alloc();
				Result value;
				if(init.has_value()){value=(*init)->print();_store(value,func_slot);}
				symbol_insert(ident,func_slot);
				const int FP_CAP_SLOTS=8;
				vector<Symbol>cap_slots(FP_CAP_SLOTS);
				for(int i=0;i<FP_CAP_SLOTS;++i){cap_slots[i]=_alloc();_store(Result(true,0),cap_slots[i]);}
				ClosureLayout cl;cl.func_slot=func_slot;cl.cap_slots=cap_slots;cl.has_self=false;cl.cap_count=0;closure_layouts[ident]=cl;
				return value;
			}
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
	else
	{
		int size=exps->size();
		array_size.resize(size);
		for(int i=0;i<size;++i)
		{
			auto now=(*exps)[i]->print();
			assert(now.imm);
			array_size[i]=now.value;
		}
		if(debug_flag)
		{
			out<<"array_size : ";
			for(int i=0;i<size;++i)
				out<<array_size[i]<<" ";
			out<<"\n";
		}
		for(int i=size-2;~i;--i)
			array_size[i]*=array_size[i+1];
		array_init.clear();
		if(init.has_value())
			(*init)->print();
		if(array_init.empty())
		{
			if(is_global)
			{
				Symbol now(true,_total++);
				symbol_insert(ident,now);
				symbol_size[now]=array_size;
				out<<"global "<<now<<" = alloc [i32, "<<array_size[0]<<"], zeroinit\n";
				return Result();
			}
		}
		int len=array_init.size();
		if(is_global)
		{
			Symbol now(true,_total++);
			symbol_insert(ident,now);
			symbol_size[now]=array_size;
			out<<"global "<<now<<" = alloc [i32, "<<len<<"], {";
			for(int i=0;i<len;++i)
			{
				if(i)
					out<<", ";
				out<<array_init[i];
			}
			out<<"}\n";
		}
		else
		{
			auto now=_alloc(array_size[0]);
			symbol_insert(ident,now);
			symbol_size[now]=array_size;
			for(int i=0;i<len;++i)
			{
				auto v=_getelemptr(now,Result(true,i));
				ptr_value.insert(v);
				_store(array_init[i],v);
			}
		}
		return Result();
	}
}
static int init_ptr=0;
Result InitValAST::print()const
{
	if(debug_flag)
		out<<"InitVal :\n";
	if(exp.has_value())
	{
		auto now=(*exp)->print();
		return now;
	}
	else
	{
		int q=array_size.size()-1;
		while(q>init_ptr&&!(array_init.size()%array_size[q-1]))
			--q;
		for(const auto &init:*inits)
		{
			++init_ptr;
			auto now=init->print();
			if(now.imm||~now.value)
				array_init.push_back(now);
			--init_ptr;
		}
		if(inits->empty())
			array_init.push_back(Result(true,0));
		while(array_init.empty()||array_init.size()%array_size[q])
			array_init.push_back(Result(true,0));
		return Result(false,-1);
	}
}


Result ExpAST::print()const
{
	if(debug_flag)
		out<<"Exp :\n";
	auto x=exp->print();
	return x;
}
static bool lval_ptr;
Result LValAST::print()const
{
	if(debug_flag)
		out<<"LVal :\n";
	auto now=symbol_table[ident];
	if(exps->empty())
	{
		auto fit=func_symbol_val.find(ident);
		if(fit!=func_symbol_val.end()&&now.value==fit->second)
		{
			lval_ptr=false;
			return Result(true,func_id[ident]);
		}
		if(symbol_size.count(now))
		{
			lval_ptr=true;
			if(ptr_value.count(now))
			{
				Symbol tmp(true,_total++);
				out<<"\t"<<tmp<<" = load "<<now<<"\n";
				ptr_value.insert(tmp);
				return tmp;
			}
			else
				return Result(now);
		}
		else
		{
			lval_ptr=false;
			return Result(now);
		}
	}
	else
	{
		auto array_size=symbol_size[now];
		int len=exps->size();
		vector<Result>index(len);
		for(int i=0;i<len;++i)
		{
			index[i]=(*exps)[i]->print();
			if(i+1<(int)array_size.size())
				index[i]=_mul(index[i],Result(true,array_size[i+1]));
		}
		auto pos=index[0];
		for(int i=1;i<len;++i)
			pos=_add(pos,index[i]);
		lval_ptr=len<array_size.size();
		if(ptr_value.count(now))
		{
			Symbol tmp(true,_total++);
			out<<"\t"<<tmp<<" = load "<<now<<"\n";
			ptr_value.insert(tmp);
			auto q=_getptr(tmp,pos);
			ptr_value.insert(q);
			return q;
		}
		else
		{
			auto tmp=_getelemptr(now,pos);
			ptr_value.insert(tmp);
			return Result(tmp);
		}
	}
}
Result UnaryExpAST::print()const
{
	if(debug_flag)
		out<<"UnaryExp :\n";
	if(func)
	{
		int size=params->size();
		vector<Result>result(size);
		for(int i=0;i<size;++i)
			result[i]=(*params)[i]->print();
		bool is_direct=false;
		string target_func;
		bool returns_int=true;
		if(fname.has_value())
		{
			auto it=symbol_table.find(*fname);
			auto fit=func_symbol_val.find(*fname);
			if(builtin_funcs.count(*fname)||(it!=symbol_table.end()&&fit!=func_symbol_val.end()&&it->second.value==fit->second))
			{
				is_direct=true;
				target_func=*fname;
				returns_int=func_table[*fname];
			}
		}
		if(is_direct)
		{
			string str;
			if(builtin_funcs.count(target_func))
				str="@"+target_func;
			else
				str=symbol_table[target_func];
			if(returns_int)
			{
				Result now(false,_total++);
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
		if(fname.has_value()&&self_params.count(*fname))
		{
			Symbol self_sym=symbol_table[*fname];
			Result self_val=_load(self_sym);
			string lambda_sym=string(Symbol(true,func_symbol_val[current_lambda_func_name]));
			returns_int=!func_table[current_lambda_func_name]?false:true;
			check_ex();
			if(returns_int)
			{
				Result now(false,_total++);
				out<<"\t"<<now<<" = call "<<lambda_sym<<"(";
				out<<self_val;
				for(int i=1;i<size;++i) out<<", "<<result[i];
				out<<")\n";
				need_jump=true;
				return now;
			}
			else
			{
				out<<"\tcall "<<lambda_sym<<"(";
				out<<self_val;
				for(int i=1;i<size;++i) out<<", "<<result[i];
				out<<")\n";
				need_jump=true;
				return Result();
			}
		}
		Result callee_val;
		if(fname.has_value()){auto sym=symbol_table[*fname];callee_val=_load(sym);}
		else callee_val=exp->print();
		vector<Result>cap_vals;bool has_slf=false;
		if(fname.has_value()){auto cl=closure_layouts.find(*fname);if(cl!=closure_layouts.end()){if(!cl->second.lambda_func_name.empty()||cl->second.has_self||cl->second.has_captures){int n=cl->second.cap_count>0?cl->second.cap_count:(int)cl->second.cap_slots.size();for(int i=0;i<n;++i)cap_vals.push_back(_load(cl->second.cap_slots[i]));}if(cl->second.has_self)has_slf=true;}}
		{
			bool has_int=false,has_void=false;
			for(auto &[fn,sig]:func_sigs)
			{
				if(builtin_funcs.count(fn))
					continue;
				if(sig.param_count!=size)
					continue;
				if(sig.returns_int)
					has_int=true;
				else
					has_void=true;
			}
			if(has_int)
				returns_int=true;
			else if(has_void)
				returns_int=false;
			else
				returns_int=true;
		}
		string ret_str=returns_int?"i32":"void";
		string sentinel="@__fp_"+to_string(size+cap_vals.size())+"_"+ret_str;
		check_ex();
		if(returns_int){Result now(false,_total++);out<<"\t"<<now<<" = call "<<sentinel<<"(";out<<callee_val;if(has_slf)out<<", 0";for(auto &cv:cap_vals)out<<", "<<cv;for(int i=has_slf?1:0;i<size;++i)out<<", "<<result[i];out<<")\n";need_jump=true;return now;}
		else{out<<"\tcall "<<sentinel<<"(";out<<callee_val;if(has_slf)out<<", 0";for(auto &cv:cap_vals)out<<", "<<cv;for(int i=has_slf?1:0;i<size;++i)out<<", "<<result[i];out<<")\n";need_jump=true;return Result();}
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
			if(x.imm)
			{
				Result now(true);
				if(op.value()=="-")
					now.value=-x.value;
				if(op.value()=="!")
					now.value=!x.value;
				return now;
			}
			else
			{
				Result now(false,_total++);
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
			auto now=Symbol((*lval)->print());
			if(now.addr)
			{
				if(lval_ptr)
				{
					if(!ptr_value.count(now))
					{
						auto tmp=_getelemptr(now,Result(true,0));
						ptr_value.insert(tmp);
						return tmp;
					}
					else
						return now;
				}
				else
					return _load(now); 
			}
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
		if(y.imm)
		{
			if(value->second=="*")
			{
				if(y.value==1) return x;
				if(y.value==0) return Result(true,0);
			}
			if(value->second=="/")
			{
				if(y.value==1) return x;
			}
			if(value->second=="%")
			{
				if(y.value==1) return Result(true,0);
			}
		}
		if(x.imm)
		{
			if(value->second=="*")
			{
				if(x.value==1) return y;
				if(x.value==0) return Result(true,0);
			}
			if(value->second=="/")
			{
				if(x.value==0) return Result(true,0);
			}
			if(value->second=="%")
			{
				if(x.value==0) return Result(true,0);
			}
		}
		Result now(false,_total++);
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
		if(y.imm&&y.value==0)
			return x;
		if(x.imm&&x.value==0&&value->second=="+")
			return y;
		Result now(false,_total++);
		check_ex();
		if(value->second=="+")
			out<<"\t"<<now<<" = add "<<x<<", "<<y<<"\n";
		if(value->second=="-")
			out<<"\t"<<now<<" = sub "<<x<<", "<<y<<"\n";
		need_jump=true;
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
			Result now(false,_total++);
			check_ex();
			if(value->second=="<")
				out<<"\t"<<now<<" = lt "<<x<<", "<<y<<"\n";
			if(value->second==">")
				out<<"\t"<<now<<" = gt "<<x<<", "<<y<<"\n";
			if(value->second=="<=")
				out<<"\t"<<now<<" = le "<<x<<", "<<y<<"\n";
			if(value->second==">=")
				out<<"\t"<<now<<" = ge "<<x<<", "<<y<<"\n";
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
			Result now(false,_total++);
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
				if(y.imm)
					return Result(true,(bool)y.value);
				else
				{
					Result v(false,_total++);
					out<<"\t"<<v<<" = ne "<<y<<", 0\n";
					return v;
				}
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
				Result v(false,_total++);
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
				if(y.imm)
					return Result(true,(bool)y.value);
				else
				{
					Result v(false,_total++);
					out<<"\t"<<v<<" = ne "<<y<<", 0\n";
					return v;
				}
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
				Result v(false,_total++);
				out<<"\t"<<v<<" = ne "<<y<<", 0\n";
				_store(v,p);
			}
			add_basic_block(_if_end(i));
			return _load(p);
		}
	}
}