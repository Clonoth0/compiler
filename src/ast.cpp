#include<cassert>
#include<algorithm>
#include<map>
#include<set>
#include<unordered_map>
#include<unordered_set>
#include<tuple>
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
	bool all_i32_params;
	bool has_self_param;
};
static constexpr int FP_CAP_SLOTS=8;
static constexpr int FP_ENV_BASE=1000000;
static constexpr int FP_ENV_LIMIT=256;
static constexpr int FP_ENV_WORDS=1+FP_CAP_SLOTS;
static unordered_map<string,int>func_id;
static unordered_map<string,int>func_symbol_val;
static unordered_map<string,FuncSig>func_sigs;
static int next_func_id=0;
static int next_lambda_id=0;
static int fp_call_total=0;
static unordered_map<string,ClosureLayout>closure_layouts;
static unordered_set<string>self_params;
static string current_lambda_func_name;
static bool in_auto_def=false;
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
static Result _add(const Result &lhs,const Result &rhs);
static Result _mul(const Result &lhs,const Result &rhs);
static Result fp_env_store_new(const Result &func_value,const vector<Result> &captures)
{
	Result sp=_load(Symbol(true,func_symbol_val["__fp_env_sp"]));
	Result env_id=_add(sp,Result(true,FP_ENV_BASE));
	auto func_slot=_getelemptr(Symbol(true,func_symbol_val["__fp_env"]),_mul(sp,Result(true,FP_ENV_WORDS)));
	ptr_value.insert(func_slot);
	_store(func_value,func_slot);
	for(int i=0;i<FP_CAP_SLOTS;++i)
	{
		auto slot=_getelemptr(Symbol(true,func_symbol_val["__fp_env"]),_add(_mul(sp,Result(true,FP_ENV_WORDS)),Result(true,i+1)));
		ptr_value.insert(slot);
		if(i<(int)captures.size())
			_store(captures[i],slot);
		else
			_store(Result(true,0),slot);
	}
	_store(_add(sp,Result(true,1)),Symbol(true,func_symbol_val["__fp_env_sp"]));
	return env_id;
}
static void emit_fp_helper(int n,bool returns_int)
{
	vector<pair<int,string>>cands;
	vector<tuple<int,string,int>>env_cands;
	for(const auto &[name,id]:func_id)
	{
		auto it=func_sigs.find(name);
		if(it==func_sigs.end())
			continue;
		if(builtin_funcs.count(name))
			continue;
		if(it->second.returns_int!=returns_int||!it->second.all_i32_params||it->second.has_self_param)
			continue;
		if(it->second.param_count==n)
			cands.emplace_back(id,name);
		if(it->second.param_count>=n&&it->second.param_count<=n+FP_CAP_SLOTS)
			env_cands.emplace_back(id,name,it->second.param_count-n);
	}
	sort(cands.begin(),cands.end());
	sort(env_cands.begin(),env_cands.end());
	string helper_name="__fp_"+to_string(n)+"_"+(returns_int?"i32":"void");
	string default_label="%"+helper_name+"_default";
	out<<"fun @"+helper_name<<"(%fid: i32";
	for(int i=0;i<n;++i)
		out<<", %a"<<i<<": i32";
	out<<")"<<(returns_int?" : i32":"")<<" {\n";
	out<<"%entry_"+helper_name<<":\n";
	if(!env_cands.empty())
	{
		string direct_label="%"+helper_name+"_direct";
		string env_label="%"+helper_name+"_env";
		Result is_env(false,_total++);
		out<<"\t"<<is_env<<" = ge %fid, "<<FP_ENV_BASE<<"\n";
		out<<"\tbr "<<is_env<<", "<<env_label<<", "<<direct_label<<"\n";
		out<<env_label<<":\n";
		Result row(false,_total++);
		out<<"\t"<<row<<" = sub %fid, "<<FP_ENV_BASE<<"\n";
		Result base_pos(false,_total++);
		out<<"\t"<<base_pos<<" = mul "<<row<<", "<<FP_ENV_WORDS<<"\n";
		Symbol func_slot(true,_total++);
		out<<"\t"<<func_slot<<" = getelemptr "<<Symbol(true,func_symbol_val["__fp_env"])<<", "<<base_pos<<"\n";
		Result real_fid(false,_total++);
		out<<"\t"<<real_fid<<" = load "<<func_slot<<"\n";
		vector<Result>caps;
		for(int i=0;i<FP_CAP_SLOTS;++i)
		{
			Result pos(false,_total++);
			out<<"\t"<<pos<<" = add "<<base_pos<<", "<<(i+1)<<"\n";
			Symbol slot(true,_total++);
			out<<"\t"<<slot<<" = getelemptr "<<Symbol(true,func_symbol_val["__fp_env"])<<", "<<pos<<"\n";
			Result cap(false,_total++);
			out<<"\t"<<cap<<" = load "<<slot<<"\n";
			caps.push_back(cap);
		}
		string env_default="%"+helper_name+"_env_default";
		if(!env_cands.empty())
			out<<"\tjump %"<<helper_name<<"_env_check_0\n";
		for(size_t idx=0;idx<env_cands.size();++idx)
		{
			auto [id,name,hidden]=env_cands[idx];
			string check_label="%"+helper_name+"_env_check_"+to_string(idx);
			string case_label="%"+helper_name+"_env_case_"+to_string(idx);
			string next_label=(idx+1<env_cands.size())?"%"+helper_name+"_env_check_"+to_string(idx+1):env_default;
			out<<check_label<<":\n";
			Result cond(false,_total++);
			out<<"\t"<<cond<<" = eq "<<real_fid<<", "<<id<<"\n";
			out<<"\tbr "<<cond<<", "<<case_label<<", "<<next_label<<"\n";
			out<<case_label<<":\n";
			string target=string(Symbol(true,func_symbol_val[name]));
			if(returns_int)
			{
				Result now(false,_total++);
				out<<"\t"<<now<<" = call "<<target<<"(";
				bool first=true;
				for(int i=0;i<hidden;++i)
				{
					if(!first)
						out<<", ";
					first=false;
					out<<caps[i];
				}
				for(int i=0;i<n;++i)
				{
					if(!first)
						out<<", ";
					first=false;
					out<<"%a"<<i;
				}
				out<<")\n";
				out<<"\tret "<<now<<"\n";
			}
			else
			{
				out<<"\tcall "<<target<<"(";
				bool first=true;
				for(int i=0;i<hidden;++i)
				{
					if(!first)
						out<<", ";
					first=false;
					out<<caps[i];
				}
				for(int i=0;i<n;++i)
				{
					if(!first)
						out<<", ";
					first=false;
					out<<"%a"<<i;
				}
				out<<")\n";
				out<<"\tret\n";
			}
		}
		out<<env_default<<":\n";
		if(returns_int)
			out<<"\tret 0\n";
		else
			out<<"\tret\n";
		out<<direct_label<<":\n";
		if(cands.empty())
			out<<"\tjump "<<default_label<<"\n";
		else
			out<<"\tjump %"<<helper_name<<"_check_0\n";
	}
	else if(cands.empty())
		out<<"\tjump "<<default_label<<"\n";
	else
		out<<"\tjump %"<<helper_name<<"_check_0\n";
	if(cands.empty())
	{
		out<<default_label<<":\n";
		if(returns_int)
			out<<"\tret 0\n";
		else
			out<<"\tret\n";
		out<<"}\n";
		return;
	}
	for(size_t idx=0;idx<cands.size();++idx)
	{
		string check_label="%"+helper_name+"_check_"+to_string(idx);
		string case_label="%"+helper_name+"_case_"+to_string(idx);
		string next_label=(idx+1<cands.size())?"%"+helper_name+"_check_"+to_string(idx+1):default_label;
		out<<check_label<<":\n";
		Result cond(false,_total++);
		out<<"\t"<<cond<<" = eq %fid, "<<cands[idx].first<<"\n";
		out<<"\tbr "<<cond<<", "<<case_label<<", "<<next_label<<"\n";
		out<<case_label<<":\n";
		string target=string(Symbol(true,func_symbol_val[cands[idx].second]));
		if(returns_int)
		{
			Result now(false,_total++);
			out<<"\t"<<now<<" = call "<<target<<"(";
			for(int i=0;i<n;++i)
			{
				if(i)
					out<<", ";
				out<<"%a"<<i;
			}
			out<<")\n";
			out<<"\tret "<<now<<"\n";
		}
		else
		{
			out<<"\tcall "<<target<<"(";
			for(int i=0;i<n;++i)
			{
				if(i)
					out<<", ";
				out<<"%a"<<i;
			}
			out<<")\n";
			out<<"\tret\n";
		}
	}
	out<<default_label<<":\n";
	if(returns_int)
		out<<"\tret 0\n";
	else
		out<<"\tret\n";
	out<<"}\n";
}
static vector<tuple<int,string,int>> fp_dispatch_candidates(int n,bool returns_int,bool env)
{
	vector<tuple<int,string,int>>cands;
	for(const auto &[name,id]:func_id)
	{
		auto it=func_sigs.find(name);
		if(it==func_sigs.end())
			continue;
		const auto &sig=it->second;
		if(builtin_funcs.count(name)||sig.has_self_param||!sig.all_i32_params||sig.returns_int!=returns_int)
			continue;
		if(env)
		{
			if(sig.param_count>=n&&sig.param_count<=n+FP_CAP_SLOTS)
				cands.emplace_back(id,name,sig.param_count-n);
		}
		else if(sig.param_count==n)
			cands.emplace_back(id,name,0);
	}
	sort(cands.begin(),cands.end());
	return cands;
}
static void emit_dispatch_chain(const Result &fid,const vector<tuple<int,string,int>> &cands,const vector<Result> &hidden,const vector<Result> &args,bool returns_int,const optional<Symbol> &ret_slot,const string &done,const string &prefix)
{
	if(cands.empty())
	{
		if(returns_int)
			_store(Result(true,0),*ret_slot);
		check_ex();
		out<<"\tjump "<<done<<"\n";
		need_jump=false;
		return;
	}
	check_ex();
	out<<"\tjump "<<prefix<<"_check_0\n";
	need_jump=false;
	for(size_t idx=0;idx<cands.size();++idx)
	{
		string check_label=prefix+"_check_"+to_string(idx);
		string case_label=prefix+"_case_"+to_string(idx);
		string next_label=(idx+1<cands.size())?prefix+"_check_"+to_string(idx+1):prefix+"_default";
		add_basic_block(check_label);
		auto [id,name,hidden_count]=cands[idx];
		Result cond(false,_total++);
		out<<"\t"<<cond<<" = eq "<<fid<<", "<<id<<"\n";
		out<<"\tbr "<<cond<<", "<<case_label<<", "<<next_label<<"\n";
		need_jump=false;
		add_basic_block(case_label);
		string target=string(Symbol(true,func_symbol_val[name]));
		if(returns_int)
		{
			Result now(false,_total++);
			out<<"\t"<<now<<" = call "<<target<<"(";
			bool first=true;
			for(int i=0;i<hidden_count&&i<(int)hidden.size();++i)
			{
				if(!first)
					out<<", ";
				first=false;
				out<<hidden[i];
			}
			for(auto &arg:args)
			{
				if(!first)
					out<<", ";
				first=false;
				out<<arg;
			}
			out<<")\n";
			_store(now,*ret_slot);
		}
		else
		{
			out<<"\tcall "<<target<<"(";
			bool first=true;
			for(int i=0;i<hidden_count&&i<(int)hidden.size();++i)
			{
				if(!first)
					out<<", ";
				first=false;
				out<<hidden[i];
			}
			for(auto &arg:args)
			{
				if(!first)
					out<<", ";
				first=false;
				out<<arg;
			}
			out<<")\n";
		}
		check_ex();
		out<<"\tjump "<<done<<"\n";
		need_jump=false;
	}
	add_basic_block(prefix+"_default");
	if(returns_int)
		_store(Result(true,0),*ret_slot);
	check_ex();
	out<<"\tjump "<<done<<"\n";
	need_jump=false;
}
static Result emit_fp_call_inline(const Result &callee_val,const vector<Result> &args,bool returns_int)
{
	int id=fp_call_total++;
	string env_label="%fp_env_"+to_string(id);
	string direct_label="%fp_direct_"+to_string(id);
	string done="%fp_done_"+to_string(id);
	optional<Symbol>ret_slot;
	if(returns_int)
		ret_slot=_alloc();
	check_ex();
	Result is_env(false,_total++);
	out<<"\t"<<is_env<<" = ge "<<callee_val<<", "<<FP_ENV_BASE<<"\n";
	out<<"\tbr "<<is_env<<", "<<env_label<<", "<<direct_label<<"\n";
	need_jump=false;
	add_basic_block(env_label);
	Result row(false,_total++);
	out<<"\t"<<row<<" = sub "<<callee_val<<", "<<FP_ENV_BASE<<"\n";
	Result base_pos(false,_total++);
	out<<"\t"<<base_pos<<" = mul "<<row<<", "<<FP_ENV_WORDS<<"\n";
	Symbol func_slot(true,_total++);
	out<<"\t"<<func_slot<<" = getelemptr "<<Symbol(true,func_symbol_val["__fp_env"])<<", "<<base_pos<<"\n";
	Result real_fid(false,_total++);
	out<<"\t"<<real_fid<<" = load "<<func_slot<<"\n";
	vector<Result>hidden;
	for(int i=0;i<FP_CAP_SLOTS;++i)
	{
		Result pos(false,_total++);
		out<<"\t"<<pos<<" = add "<<base_pos<<", "<<(i+1)<<"\n";
		Symbol slot(true,_total++);
		out<<"\t"<<slot<<" = getelemptr "<<Symbol(true,func_symbol_val["__fp_env"])<<", "<<pos<<"\n";
		Result cap(false,_total++);
		out<<"\t"<<cap<<" = load "<<slot<<"\n";
		hidden.push_back(cap);
	}
	emit_dispatch_chain(real_fid,fp_dispatch_candidates((int)args.size(),returns_int,true),hidden,args,returns_int,ret_slot,done,"%fp_env_dispatch_"+to_string(id));
	add_basic_block(direct_label);
	vector<Result>no_hidden;
	emit_dispatch_chain(callee_val,fp_dispatch_candidates((int)args.size(),returns_int,false),no_hidden,args,returns_int,ret_slot,done,"%fp_direct_dispatch_"+to_string(id));
	add_basic_block(done);
	if(returns_int)
		return _load(*ret_slot);
	return Result();
}
static void visit_lambdas(const node &n,vector<const LambdaExpAST*> &order,unordered_set<const LambdaExpAST*> &seen)
{
	if(!n)
		return;
	if(auto l=dynamic_cast<LambdaExpAST*>(n.get()))
	{
		l->pre_register();
		if(l->block)
			visit_lambdas(l->block,order,seen);
		if(!seen.count(l))
		{
			seen.insert(l);
			order.push_back(l);
		}
		return;
	}
	if(auto p=dynamic_cast<PrimaryExpAST*>(n.get()))
	{
		if(p->lval.has_value())
			visit_lambdas(*p->lval,order,seen);
		if(p->exp)
			visit_lambdas(p->exp,order,seen);
		return;
	}
	if(auto u=dynamic_cast<UnaryExpAST*>(n.get()))
	{
		if(u->exp)
			visit_lambdas(u->exp,order,seen);
		if(u->params)
			for(auto &x:*u->params)
				visit_lambdas(x,order,seen);
		return;
	}
	if(auto b=dynamic_cast<MulExpAST*>(n.get())){visit_lambdas(b->unary_exp,order,seen);if(b->value.has_value())visit_lambdas(b->value->first,order,seen);return;}
	if(auto a=dynamic_cast<AddExpAST*>(n.get())){visit_lambdas(a->mul_exp,order,seen);if(a->value.has_value())visit_lambdas(a->value->first,order,seen);return;}
	if(auto r=dynamic_cast<RelExpAST*>(n.get())){visit_lambdas(r->add_exp,order,seen);if(r->value.has_value())visit_lambdas(r->value->first,order,seen);return;}
	if(auto e=dynamic_cast<EqExpAST*>(n.get())){visit_lambdas(e->rel_exp,order,seen);if(e->value.has_value())visit_lambdas(e->value->first,order,seen);return;}
	if(auto nd=dynamic_cast<AndExpAST*>(n.get())){visit_lambdas(nd->eq_exp,order,seen);if(nd->value.has_value())visit_lambdas(nd->value->first,order,seen);return;}
	if(auto o=dynamic_cast<OrExpAST*>(n.get())){visit_lambdas(o->and_exp,order,seen);if(o->value.has_value())visit_lambdas(o->value->first,order,seen);return;}
	if(auto e=dynamic_cast<ExpAST*>(n.get())){visit_lambdas(e->exp,order,seen);return;}
	if(auto lv=dynamic_cast<LValAST*>(n.get())){if(lv->exps)for(auto &x:*lv->exps)visit_lambdas(x,order,seen);return;}
	if(auto s=dynamic_cast<MatchedStmtAST*>(n.get())){if(s->lval.has_value())visit_lambdas(*s->lval,order,seen);if(s->exp.has_value())visit_lambdas(*s->exp,order,seen);if(s->block.has_value())visit_lambdas(*s->block,order,seen);if(s->stmt1)visit_lambdas(s->stmt1,order,seen);if(s->stmt2)visit_lambdas(s->stmt2,order,seen);return;}
	if(auto d=dynamic_cast<DanglingStmtAST*>(n.get())){visit_lambdas(d->exp,order,seen);if(d->stmt1)visit_lambdas(d->stmt1,order,seen);if(d->stmt2)visit_lambdas(d->stmt2,order,seen);return;}
	if(auto s=dynamic_cast<StmtAST*>(n.get())){visit_lambdas(s->stmt,order,seen);return;}
	if(auto b=dynamic_cast<BlockAST*>(n.get())){if(b->blocks)for(auto &x:*b->blocks)visit_lambdas(x,order,seen);return;}
	if(auto bi=dynamic_cast<BlockItemAST*>(n.get())){visit_lambdas(bi->block,order,seen);return;}
	if(auto vd=dynamic_cast<VarDefAST*>(n.get())){if(vd->init.has_value())visit_lambdas(*vd->init,order,seen);if(vd->exps)for(auto &x:*vd->exps)visit_lambdas(x,order,seen);return;}
	if(auto d=dynamic_cast<DeclAST*>(n.get())){visit_lambdas(d->decl,order,seen);return;}
	if(auto v=dynamic_cast<VarDeclAST*>(n.get())){if(v->defs)for(auto &x:*v->defs)visit_lambdas(x,order,seen);return;}
	if(auto cd=dynamic_cast<ConstDefAST*>(n.get())){if(cd->init)visit_lambdas(cd->init,order,seen);if(cd->exps)for(auto &x:*cd->exps)visit_lambdas(x,order,seen);return;}
	if(auto cd=dynamic_cast<ConstDeclAST*>(n.get())){if(cd->defs)for(auto &x:*cd->defs)visit_lambdas(x,order,seen);return;}
	if(auto i=dynamic_cast<InitValAST*>(n.get())){if(i->exp.has_value())visit_lambdas(*i->exp,order,seen);if(i->inits)for(auto &x:*i->inits)visit_lambdas(x,order,seen);return;}
	if(auto fd=dynamic_cast<FuncDefAST*>(n.get())){if(fd->block)visit_lambdas(fd->block,order,seen);return;}
	if(auto f=dynamic_cast<FuncFParamAST*>(n.get())){if(f->exps)for(auto &x:*f->exps)visit_lambdas(x,order,seen);return;}
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
	if(!func_symbol_val.count("__fp_env_sp"))
	{
		Symbol sp_sym(true,_total++);
		Symbol env_sym(true,_total++);
		func_symbol_val["__fp_env_sp"]=sp_sym.value;
		func_symbol_val["__fp_env"]=env_sym.value;
		symbol_table["__fp_env_sp"]=sp_sym;
		symbol_table["__fp_env"]=env_sym;
	}
	for(const auto &def:*defs)
		def->pre_register();
	out<<"global "<<Symbol(true,func_symbol_val["__fp_env_sp"])<<" = alloc i32, zeroinit\n";
	out<<"global "<<Symbol(true,func_symbol_val["__fp_env"])<<" = alloc [i32, "<<(FP_ENV_LIMIT*FP_ENV_WORDS)<<"], zeroinit\n";
	vector<const LambdaExpAST*>lambda_emit_order;
	unordered_set<const LambdaExpAST*>seen_lambdas;
	for(const auto &def:*defs)
		visit_lambdas(def,lambda_emit_order,seen_lambdas);
	unordered_map<const LambdaExpAST*,int>lambda_index,indeg;
	unordered_map<const LambdaExpAST*,vector<const LambdaExpAST*>>lambda_next;
	for(size_t i=0;i<lambda_emit_order.size();++i)
	{
		lambda_index[lambda_emit_order[i]]=(int)i;
		indeg[lambda_emit_order[i]]=0;
	}
	auto add_lambda_edge=[&](const LambdaExpAST *before,const LambdaExpAST *after)
	{
		if(!before||!after||before==after)
			return;
		if(!lambda_index.count(before)||!lambda_index.count(after))
			return;
		auto &next=lambda_next[before];
		if(find(next.begin(),next.end(),after)!=next.end())
			return;
		next.push_back(after);
		++indeg[after];
	};
	for(const auto *lambda:lambda_emit_order)
	{
		vector<const LambdaExpAST*>nested;
		unordered_set<const LambdaExpAST*>seen_nested;
		if(lambda->block)
			visit_lambdas(lambda->block,nested,seen_nested);
		for(const auto *child:nested)
			add_lambda_edge(child,lambda);
	}
	vector<const LambdaExpAST*>ordered_lambdas;
	unordered_set<const LambdaExpAST*>emitted_lambdas;
	auto better_lambda=[&](const LambdaExpAST *a,const LambdaExpAST *b)
	{
		if(a->has_self!=b->has_self)
			return !a->has_self;
		if(a->captures.size()!=b->captures.size())
			return a->captures.size()<b->captures.size();
		return lambda_index[a]<lambda_index[b];
	};
	while(ordered_lambdas.size()<lambda_emit_order.size())
	{
		const LambdaExpAST *best=nullptr;
		for(const auto *lambda:lambda_emit_order)
		{
			if(emitted_lambdas.count(lambda)||indeg[lambda]!=0)
				continue;
			if(!best||better_lambda(lambda,best))
				best=lambda;
		}
		if(!best)
		{
			for(const auto *lambda:lambda_emit_order)
				if(!emitted_lambdas.count(lambda))
				{
					best=lambda;
					break;
				}
		}
		emitted_lambdas.insert(best);
		ordered_lambdas.push_back(best);
		for(const auto *next:lambda_next[best])
			--indeg[next];
	}
	lambda_emit_order=ordered_lambdas;
	symbol_stack.push_back({});
	auto emit=[&](bool fp,bool include_main)
	{
		for(const auto &def:*defs)
		{
			if(def->has_func_ptr_params()==fp)
			{
				auto fd=dynamic_cast<FuncDefAST*>(def.get());
				if(fd&&fd->ident=="main"&&!include_main)
					continue;
				if(fd&&fd->ident!="main"&&include_main)
					continue;
				if(fd&&!builtin_funcs.count(fd->ident))
					func_id[fd->ident]=next_func_id++;
				is_global=true;
				def->print();
			}
		}
	};
	emit(false,false);
	for(const auto *lambda:lambda_emit_order)
		lambda->print_body();
	for(int n=0;n<=16;++n)
	{
		emit_fp_helper(n,true);
		emit_fp_helper(n,false);
	}
	emit(true,false);
	emit(false,true);
	return Result();
}
static vector<pair<Symbol,bool>>def_params; // ptr : true, value : false
void FuncDefAST::pre_register()const
{
	bool all_i32_params=true;
	for(const auto &param:*params)
	{
		auto fp=dynamic_cast<FuncFParamAST*>(param.get());
		if(fp&&fp->ptr)
		{
			all_i32_params=false;
			break;
		}
	}
	func_sigs[ident]={(int)params->size(),!type.empty(),all_i32_params,false};
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
static void walk_decls(const node &n,unordered_set<string>&declared);
static void walk_ast(const node &n,unordered_set<string>&found)
{
	if(!n) return;
	if(auto lv=dynamic_cast<LValAST*>(n.get())){found.insert(lv->ident);return;}
	if(auto p=dynamic_cast<PrimaryExpAST*>(n.get())){if(p->lval.has_value())walk_ast(*p->lval,found);else if(p->exp)walk_ast(p->exp,found);return;}
	if(auto u=dynamic_cast<UnaryExpAST*>(n.get())){if(u->func){if(u->fname.has_value())found.insert(*u->fname);if(u->params)for(auto &x:*u->params)walk_ast(x,found);else if(u->exp)walk_ast(u->exp,found);}else if(u->exp)walk_ast(u->exp,found);return;}
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
	if(auto l=dynamic_cast<LambdaExpAST*>(n.get()))
	{
		unordered_set<string>inner_found;
		if(l->block)
			walk_ast(l->block,inner_found);
		unordered_set<string>inner_params;
		for(auto &p:*l->params)
		{
			auto lp=dynamic_cast<LambdaParamAST*>(p.get());
			inner_params.insert(lp->ident);
		}
		if(l->has_self)
			inner_params.insert(l->self_name);
		unordered_set<string>inner_declared;
		if(l->block)
			walk_decls(l->block,inner_declared);
		for(auto &id:inner_found)
			if(!inner_params.count(id)&&!inner_declared.count(id))
				found.insert(id);
		return;
	}
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
	func_sigs[lambda_name]={total_params,!type.empty(),true,has_self};
	func_table[lambda_name]=!type.empty();
	if(!builtin_funcs.count(lambda_name)){Symbol now(true,_total++);symbol_table[lambda_name]=now;func_symbol_val[lambda_name]=now.value;}
}
Result LambdaExpAST::print()const
{
	if(lambda_name.empty())
		pre_register();
	if(capture_count()>0&&!in_auto_def)
	{
		vector<Result>cap_vals;
		for(const auto &cap_name:captures)
		{
			Symbol var_sym;
			auto sym_it=symbol_table.find(cap_name);
			if(sym_it!=symbol_table.end())
				var_sym=sym_it->second;
			if(!var_sym.addr)
			{
				auto cl=closure_layouts.find(current_lambda_func_name);
				if(cl!=closure_layouts.end())
				{
					for(size_t ci=0;ci<cl->second.capture_names.size()&&ci<cl->second.cap_slots.size();++ci)
					{
						if(cl->second.capture_names[ci]==cap_name)
						{
							var_sym=cl->second.cap_slots[ci];
							break;
						}
					}
				}
			}
			if(var_sym.addr)
				cap_vals.push_back(_load(var_sym));
			else if(func_id.count(cap_name))
				cap_vals.push_back(Result(true,func_id[cap_name]));
			else
				cap_vals.push_back(Result(true,0));
		}
		return fp_env_store_new(Result(true,func_id[lambda_name]),cap_vals);
	}
	return Result(true,func_id[lambda_name]);
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
	for(const auto &p:*params)
	{
		auto lp=dynamic_cast<LambdaParamAST*>(p.get());
		if(lp->is_self)
			continue;
		if(!first)
			out<<", ";
		first=false;
		Symbol now(true,_total++);
		symbol_insert(lp->ident,now);
		out<<now<<"_: i32";
	}
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
		if(lp->is_fp)
		{
			ClosureLayout pcl;
			pcl.func_slot=now;
			closure_layouts[lp->ident]=pcl;
		}
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
		for(int i=0;i<K;++i){cap_slots[i]=_alloc();string cap_name=lambda->captures[i];Symbol var_sym;auto sym_it=symbol_table.find(cap_name);if(sym_it!=symbol_table.end())var_sym=sym_it->second;if(!var_sym.addr){auto cl=closure_layouts.find(current_lambda_func_name);if(cl!=closure_layouts.end()){auto &cn=cl->second.capture_names;auto &cs=cl->second.cap_slots;for(size_t ci=0;ci<cn.size();++ci)if(cn[ci]==cap_name&&ci<cs.size()){var_sym=cs[ci];break;}}}if(!var_sym.addr){if(func_id.count(cap_name))_store(Result(true,func_id[cap_name]),cap_slots[i]);continue;}Result val=_load(var_sym);_store(val,cap_slots[i]);}
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
			vector<Result>current_caps;
			auto self_layout=closure_layouts.find(current_lambda_func_name);
			if(self_layout!=closure_layouts.end())
			{
				for(auto &slot:self_layout->second.cap_slots)
					current_caps.push_back(_load(slot));
			}
			check_ex();
			if(returns_int)
			{
				Result now(false,_total++);
				out<<"\t"<<now<<" = call "<<lambda_sym<<"(";
				out<<self_val;
				for(auto &cap:current_caps)
					out<<", "<<cap;
				for(int i=1;i<size;++i) out<<", "<<result[i];
				out<<")\n";
				need_jump=true;
				return now;
			}
			else
			{
				out<<"\tcall "<<lambda_sym<<"(";
				out<<self_val;
				for(auto &cap:current_caps)
					out<<", "<<cap;
				for(int i=1;i<size;++i) out<<", "<<result[i];
				out<<")\n";
				need_jump=true;
				return Result();
			}
		}
		if(fname.has_value())
		{
			auto cl=closure_layouts.find(*fname);
			if(cl!=closure_layouts.end()&&!cl->second.lambda_func_name.empty())
			{
				const auto &layout=cl->second;
				string lambda_sym=string(Symbol(true,func_symbol_val[layout.lambda_func_name]));
				returns_int=func_table[layout.lambda_func_name];
				vector<Result>cap_vals;
				int n=layout.cap_count>0?layout.cap_count:(int)layout.cap_slots.size();
				for(int i=0;i<n;++i)
					cap_vals.push_back(_load(layout.cap_slots[i]));
				check_ex();
				if(returns_int)
				{
					Result now(false,_total++);
					out<<"\t"<<now<<" = call "<<lambda_sym<<"(";
					bool first=true;
					if(layout.has_self)
					{
						out<<"0";
						first=false;
					}
					for(auto &cv:cap_vals)
					{
						if(!first)
							out<<", ";
						first=false;
						out<<cv;
					}
					for(int i=layout.has_self?1:0;i<size;++i)
					{
						if(!first)
							out<<", ";
						first=false;
						out<<result[i];
					}
					out<<")\n";
					need_jump=true;
					return now;
				}
				out<<"\tcall "<<lambda_sym<<"(";
				bool first=true;
				if(layout.has_self)
				{
					out<<"0";
					first=false;
				}
				for(auto &cv:cap_vals)
				{
					if(!first)
						out<<", ";
					first=false;
					out<<cv;
				}
				for(int i=layout.has_self?1:0;i<size;++i)
				{
					if(!first)
						out<<", ";
					first=false;
					out<<result[i];
				}
				out<<")\n";
				need_jump=true;
				return Result();
			}
		}
		if(!fname.has_value()&&exp)
		{
			if(auto l=dynamic_cast<LambdaExpAST*>(exp.get()))
			{
				if(l->lambda_name.empty())
					l->pre_register();
				string lambda_sym=string(Symbol(true,func_symbol_val[l->lambda_name]));
				returns_int=func_table[l->lambda_name];
				check_ex();
				if(returns_int)
				{
					Result now(false,_total++);
					out<<"\t"<<now<<" = call "<<lambda_sym<<"(";
					for(int i=0;i<size;++i)
					{
						if(i)
							out<<", ";
						out<<result[i];
					}
					out<<")\n";
					need_jump=true;
					return now;
				}
				out<<"\tcall "<<lambda_sym<<"(";
				for(int i=0;i<size;++i)
				{
					if(i)
						out<<", ";
					out<<result[i];
				}
				out<<")\n";
				need_jump=true;
				return Result();
			}
		}
		Result callee_val;
		if(fname.has_value())
		{
			auto sym_it=symbol_table.find(*fname);
			if(sym_it!=symbol_table.end()&&sym_it->second.addr)
				callee_val=_load(sym_it->second);
			else if(func_id.count(*fname))
				callee_val=Result(true,func_id[*fname]);
			else
				callee_val=Result(true,0);
		}
		else callee_val=exp->print();
		vector<Result>cap_vals;bool has_slf=false;
		if(fname.has_value()){auto cl=closure_layouts.find(*fname);if(cl!=closure_layouts.end()){if(!cl->second.lambda_func_name.empty()||cl->second.has_self||cl->second.has_captures){int n=cl->second.cap_count>0?cl->second.cap_count:(int)cl->second.cap_slots.size();for(int i=0;i<n;++i)cap_vals.push_back(_load(cl->second.cap_slots[i]));}if(cl->second.has_self)has_slf=true;}}
		{
			bool has_int=false,has_void=false;
			for(auto &[fn,sig]:func_sigs)
			{
				if(builtin_funcs.count(fn))
					continue;
				if(sig.has_self_param)
					continue;
				if(sig.param_count<size||sig.param_count>size+FP_CAP_SLOTS)
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
		vector<Result>call_args;
		if(has_slf)
			call_args.push_back(Result(true,0));
		for(auto &cv:cap_vals)
			call_args.push_back(cv);
		for(int i=has_slf?1:0;i<size;++i)
			call_args.push_back(result[i]);
		return emit_fp_call_inline(callee_val,call_args,returns_int);
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
