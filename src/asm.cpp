#include<iostream>
#include<optional>
#include<cassert>
#include<string>
#include<map>
#include<unordered_map>
#include<unordered_set>
#include<vector>
#include<algorithm>
#include"include/asm.hpp"
#include"include/koopa.h"

using namespace std;

static string last_sw_rs2,last_sw_rs1;
static int last_sw_imm;
static bool last_sw_valid;
static void _clear_peep() {last_sw_valid=false;}
static void _word(const int &value)
{
	_clear_peep();
	cout<<"\t.word "<<value<<"\n";
}
static void _ret()
{
	_clear_peep();
	cout<<"\tret\n";
}
static void _add(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\tadd "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _sub(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\tsub "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _slt(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\tslt "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _sgt(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\tsgt "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _seqz(const string &rd,const string &rs)
{
	_clear_peep();
	cout<<"\tseqz "<<rd<<", "<<rs<<"\n";
}
static void _snez(const string &rd,const string &rs)
{
	_clear_peep();
	cout<<"\tsnez "<<rd<<", "<<rs<<"\n";
}
static void _or(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\tor "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _and(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\tand "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _xor(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\txor "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _mul(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\tmul "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _div(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\tdiv "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _mod(const string &rd,const string &rs1,const string &rs2)
{
	_clear_peep();
	cout<<"\trem "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _li(const string &rd,const int &imm)
{
	_clear_peep();
	cout<<"\tli "<<rd<<", "<<imm<<"\n";
}
static void _la(const string &rd,const string &lable)
{
	_clear_peep();
	cout<<"\tla "<<rd<<", "<<lable<<"\n";
}
static void _mv(const string &rd,const string &rs)
{
	_clear_peep();
	cout<<"\tmv "<<rd<<", "<<rs<<"\n";
}
static void _lw(const string &rs,const string &rd,const int &imm)
{
	if(last_sw_valid&&rd==last_sw_rs1&&imm==last_sw_imm)
	{
		if(rs==last_sw_rs2)
			return;
		_clear_peep();
		cout<<"\tmv "<<rs<<", "<<last_sw_rs2<<"\n";
		return;
	}
	_clear_peep();
	if(imm>=-2048&&imm<=2047)
		cout<<"\tlw "<<rs<<", "<<imm<<"("<<rd<<")\n";
	else
	{
		_li("t0",imm);
		_add("t0",rd,"t0");
		_lw(rs,"t0",0);
	}
}
static void _sw(const string &rs2,const string &rs1,const int &imm)
{
	last_sw_rs2=rs2;
	last_sw_rs1=rs1;
	last_sw_imm=imm;
	last_sw_valid=true;
	if(imm>=-2048&&imm<=2047)
		cout<<"\tsw "<<rs2<<", "<<imm<<"("<<rs1<<")\n";
	else
	{
		if(rs2=="t0")
		{
			_li("a7",imm);
			_add("a7",rs1,"a7");
			_sw(rs2,"a7",0);
		}
		else
		{
			_li("t0",imm);
			_add("t0",rs1,"t0");
			_sw(rs2,"t0",0);
		}
	}
}
static void addi(const string &rd,const string &rs1,const int &imm)
{
	_clear_peep();
	if(imm>=-2048&&imm<=2047)
		cout<<"\taddi "<<rd<<", "<<rs1<<", "<<imm<<"\n";
	else
	{
		if(rd=="t0")
		{
			_li("a7",imm);
			_add(rd,rs1,"a7");
		}
		else
		{
			_li("t0",imm);
			_add(rd,rs1,"t0");
		}
	}
}
class AddressTable
{
	private:
		unordered_map<koopa_raw_value_t,int>f;
	public:
		int R,S,A,T,i;
		int query(const koopa_raw_value_t &v,int size=4)
		{
			auto p=f.find(v);
			if(p!=f.end())
				return p->second;
			f[v]=i;
			i+=size;
			return i-size;
		}
		void init(int _R,int _S,int _A)
		{
			R=_R,S=_S,A=_A;
			T=(S+R+A+15)/16*16;
			i=A;
			unordered_map<koopa_raw_value_t,int>().swap(f);
		}
};
static AddressTable addr;
static unordered_map<koopa_raw_value_t,int>params_table;
static unordered_map<koopa_raw_value_t,string>global_table;
static unordered_set<koopa_raw_value_t>ptr_value,ptr2_value;
static int global_total=0;
static void load(const string &rd,const koopa_raw_value_t &value)
{

	if(value->kind.tag==KOOPA_RVT_INTEGER)
		_li(rd,value->kind.data.integer.value);
	else
	{
		auto p=params_table.find(value);
		if(p==params_table.end())
			_lw(rd,"sp",addr.query(value));
		else
			if(p->second<8)
				_mv(rd,"a"+to_string(p->second));
			else
				_lw(rd,"sp",addr.T+(p->second-8)*4);
	}
}
static void load_addr(const string &rd,const koopa_raw_value_t &value)
{
	if(value->kind.tag==KOOPA_RVT_INTEGER)
		_li(rd,value->kind.data.integer.value);
	else
	{
		assert(!params_table.count(value));
		auto p=global_table.find(value);
		if(p!=global_table.end())
			_la(rd,p->second);
		else
			if(ptr_value.count(value))
				_lw(rd,"sp",addr.query(value));
			else
				addi(rd,"sp",addr.query(value));
	}
}

class RegCache
{
	private:
		unordered_map<koopa_raw_value_t,string>alloc;
		unordered_set<koopa_raw_value_t>valid;
		vector<string>all_regs;
		static void _uses(koopa_raw_value_t inst,vector<koopa_raw_value_t>&uses)
		{
			const auto&k=inst->kind;
			switch(k.tag)
			{
				case KOOPA_RVT_LOAD:
					uses.push_back(k.data.load.src);break;
				case KOOPA_RVT_STORE:
					uses.push_back(k.data.store.value);
					uses.push_back(k.data.store.dest);break;
				case KOOPA_RVT_GET_PTR:
					uses.push_back(k.data.get_ptr.src);
					uses.push_back(k.data.get_ptr.index);break;
				case KOOPA_RVT_GET_ELEM_PTR:
					uses.push_back(k.data.get_elem_ptr.src);
					uses.push_back(k.data.get_elem_ptr.index);break;
				case KOOPA_RVT_BINARY:
					uses.push_back(k.data.binary.lhs);
					uses.push_back(k.data.binary.rhs);break;
				case KOOPA_RVT_BRANCH:
					uses.push_back(k.data.branch.cond);break;
				case KOOPA_RVT_CALL:
					for(size_t i=0;i<k.data.call.args.len;++i)
						uses.push_back(reinterpret_cast<koopa_raw_value_t>(k.data.call.args.buffer[i]));
					break;
				case KOOPA_RVT_RETURN:
					if(k.data.ret.value)uses.push_back(k.data.ret.value);
					break;
			}
		}
		static bool _needs_alloc(koopa_raw_value_t v)
		{
			return v->ty->tag!=KOOPA_RTT_UNIT&&
			       v->kind.tag!=KOOPA_RVT_INTEGER&&
			       v->kind.tag!=KOOPA_RVT_ALLOC&&
			       v->kind.tag!=KOOPA_RVT_GLOBAL_ALLOC&&
			       v->kind.tag!=KOOPA_RVT_ZERO_INIT&&
			       v->kind.tag!=KOOPA_RVT_UNDEF&&
			       v->kind.tag!=KOOPA_RVT_AGGREGATE;
		}
	public:
		void init(const koopa_raw_function_t &func)
		{
			alloc.clear();
			valid.clear();
			all_regs={"t1","t2","t3","t4","t5","t6"};
			if(!func->bbs.len)
				return;
			unordered_map<koopa_raw_value_t,int>def,last;
			for(int i=0;i<func->params.len;++i)
			{
				auto v=reinterpret_cast<koopa_raw_value_t>(func->params.buffer[i]);
				def[v]=-1;
			}
			int idx=0;
			for(size_t bi=0;bi<func->bbs.len;++bi)
			{
				auto bb=reinterpret_cast<koopa_raw_basic_block_t>(func->bbs.buffer[bi]);
				for(size_t ii=0;ii<bb->insts.len;++ii)
				{
					auto inst=reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[ii]);
					vector<koopa_raw_value_t>uses;
					_uses(inst,uses);
					for(auto u:uses)
						last[u]=idx;
					if(_needs_alloc(inst))
						def[inst]=idx;
					++idx;
				}
			}
			struct Interval{int def,end;koopa_raw_value_t v;};
			vector<Interval>ivs;
			for(const auto &p:def)
			{
				int e=p.second;
				if(last.count(p.first))
					e=last[p.first];
				ivs.push_back({p.second,e,p.first});
			}
			sort(ivs.begin(),ivs.end(),
				[](const Interval &a,const Interval &b){return a.def<b.def;});
			vector<string>free_regs=all_regs;
			vector<Interval>active;
			for(auto &iv:ivs)
			{
				vector<Interval>still;
				for(auto &a:active)
				{
					if(a.end<iv.def)
						free_regs.push_back(alloc[a.v]);
					else
						still.push_back(a);
				}
				active=move(still);
				if(!free_regs.empty())
				{
					string r=free_regs.back();
					free_regs.pop_back();
					alloc[iv.v]=r;
					active.push_back(iv);
					sort(active.begin(),active.end(),
						[](const Interval &a,const Interval &b){return a.end<b.end;});
				}
				else
				{
					auto &spill_cand=active.back();
					if(spill_cand.end>iv.end)
					{
						alloc[iv.v]=alloc[spill_cand.v];
						alloc[spill_cand.v]="";
						active.back()=iv;
						sort(active.begin(),active.end(),
							[](const Interval &a,const Interval &b){return a.end<b.end;});
					}
				}
			}
		}
		string alloc_pool_reg(){return "t0";}
		void free_pool_reg(const string&){}
		string get(koopa_raw_value_t v)
		{
			if(v->kind.tag==KOOPA_RVT_INTEGER)
			{
				load("t0",v);
				return "t0";
			}
			auto it=alloc.find(v);
			if(it!=alloc.end()&&!it->second.empty()&&valid.count(v))
				return it->second;
			load("t0",v);
			return "t0";
		}
		void set(koopa_raw_value_t v,const string &r)
		{
			auto it=alloc.find(v);
			if(it==alloc.end())
				return;
			if(it->second.empty())
				_sw(r,"sp",addr.query(v));
			else
				valid.insert(v);
		}
		void release(koopa_raw_value_t){}
		void ensure_free(int){}
		void flush_all()
		{
			for(auto &p:alloc)
				if(!p.second.empty()&&valid.count(p.first))
					_sw(p.second,"sp",addr.query(p.first));
			valid.clear();
		}
		void invalidate()
		{
			valid.clear();
		}
		bool is_valid(koopa_raw_value_t v)const
		{
			return valid.count(v);
		}
		string reg_of(koopa_raw_value_t v)const
		{
			auto it=alloc.find(v);
			return(it!=alloc.end())?it->second:"";
		}
		void mark_valid(koopa_raw_value_t v)
		{
			valid.insert(v);
		}
};
static RegCache rc;

void visit(const koopa_raw_program_t &program)
{
	_clear_peep();
	cout<<"\t.data\n";
	visit(program.values);
	_clear_peep();
	cout<<"\n\t.text\n";
	visit(program.funcs);
}
void visit(const koopa_raw_slice_t &slice)
{
	for(size_t i=0;i<slice.len;++i)
	{
		auto ptr=slice.buffer[i];
		switch(slice.kind)
		{
			case KOOPA_RSIK_FUNCTION:
				visit(reinterpret_cast<koopa_raw_function_t>(ptr));
				break;
			case KOOPA_RSIK_BASIC_BLOCK:
				visit(reinterpret_cast<koopa_raw_basic_block_t>(ptr));
				break;
			case KOOPA_RSIK_VALUE:
				visit(reinterpret_cast<koopa_raw_value_t>(ptr));
				break;
			default:
				assert(false);
		}
	}
}
static int get_alloc_size(const koopa_raw_type_t &ty)
{
	switch(ty->tag)
	{
		case KOOPA_RTT_UNIT:
			return 0;
		case KOOPA_RTT_FUNCTION:
			return 0;
		case KOOPA_RTT_INT32:
			return 4;
		case KOOPA_RTT_POINTER:
			return 4;
		case KOOPA_RTT_ARRAY:
			return ty->data.array.len*get_alloc_size(ty->data.array.base);
	}
	assert(false);
	return 0;
}
void visit(const koopa_raw_function_t &func)
{
	if(!func->bbs.len)
		return;
	_clear_peep();
	cout<<"\t.global "<<func->name+1<<"\n";
	_clear_peep();
	cout<<func->name+1<<":\n";
	int S=0,R=0,A=0;
	for(size_t i=0;i<func->bbs.len;++i)
	{
		auto bb=reinterpret_cast<koopa_raw_basic_block_t>(func->bbs.buffer[i]);
		S+=bb->insts.len*4;
		for(size_t j=0;j<bb->insts.len;++j)
		{
			auto inst=reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[j]);
			if(inst->ty->tag==KOOPA_RTT_UNIT)
				S-=4;
			if(inst->kind.tag==KOOPA_RVT_CALL)
			{
				R=4;
				int len=inst->kind.data.call.args.len;
				if(len-8>A)
					A=len-8;
			}
			if(inst->kind.tag==KOOPA_RVT_ALLOC)
			{
				int size=get_alloc_size(inst->ty->data.pointer.base);
				S+=size-4;
			}
		}
	}
	A*=4;
	addr.init(R,S,A);
	unordered_map<koopa_raw_value_t,int>().swap(params_table);
	rc.init(func);
	addi("sp","sp",-addr.T);
	if(R)
		_sw("ra","sp",S+A);
	for(int i=0;i<func->params.len;++i)
	{
		auto value=reinterpret_cast<koopa_raw_value_t>(func->params.buffer[i]);
		params_table[value]=i;
	}
	visit(func->bbs);
}
void visit(const koopa_raw_basic_block_t &bb)
{
	rc.invalidate();
	_clear_peep();
	cout<<bb->name+1<<":\n";
	visit(bb->insts);
}
void visit(const koopa_raw_value_t &value)
{
	const auto &kind=value->kind;
	switch(kind.tag)
	{
		case KOOPA_RVT_INTEGER:
			visit(kind.data.integer);
			break;
		case KOOPA_RVT_ALLOC:
			addr.query(value,get_alloc_size(value->ty->data.pointer.base));
			if(value->ty->data.pointer.base->tag==KOOPA_RTT_POINTER)
				ptr2_value.insert(value);
			break;
		case KOOPA_RVT_GLOBAL_ALLOC:
			visit(kind.data.global_alloc,value);
			break;
		case KOOPA_RVT_LOAD:
			visit(kind.data.load,value);
			break;
		case KOOPA_RVT_STORE:
			visit(kind.data.store);
			break;
		case KOOPA_RVT_GET_PTR:
			visit(kind.data.get_ptr,value);
			break;
		case KOOPA_RVT_GET_ELEM_PTR:
			visit(kind.data.get_elem_ptr,value);
			break;
		case KOOPA_RVT_BINARY:
			visit(kind.data.binary,value);
			break;
		case KOOPA_RVT_BRANCH:
			visit(kind.data.branch);
			break;
		case KOOPA_RVT_JUMP:
			visit(kind.data.jump);
			break;
		case KOOPA_RVT_CALL:
			visit(kind.data.call,value);
			break;
		case KOOPA_RVT_RETURN:
			visit(kind.data.ret);
			break;
		default:
			assert(false);
	}
}
void visit(const koopa_raw_integer_t &i)
{
	_clear_peep();
	cout<<i.value;
}
void visit(const koopa_raw_global_alloc_t &i,const koopa_raw_value_t &value)
{
	string name="v"+to_string(global_total++);
	_clear_peep();
	cout<<"\t.global "<<name<<"\n";
	_clear_peep();
	cout<<name<<":\n";
	if(i.init->kind.tag==KOOPA_RVT_INTEGER)
		_word(i.init->kind.data.integer.value);
	else
		if(i.init->kind.tag==KOOPA_RVT_ZERO_INIT)
		{
			_clear_peep();
			cout<<"\t.zero "<<get_alloc_size(i.init->ty)<<"\n";
		}
		else
		{
			assert(i.init->kind.tag==KOOPA_RVT_AGGREGATE);
			auto aggregate=i.init->kind.data.aggregate;
			for(int i=0;i<aggregate.elems.len;++i)
			{
				auto elem=reinterpret_cast<koopa_raw_value_t>(aggregate.elems.buffer[i]);
				assert(elem->kind.tag==KOOPA_RVT_INTEGER);
				_word(elem->kind.data.integer.value);
			}
		}

	global_table[value]=name;
}
static string read_val(koopa_raw_value_t v,const string &dest)
{
	if(v->kind.tag==KOOPA_RVT_INTEGER)
	{
		_li(dest,v->kind.data.integer.value);
		return dest;
	}
	auto it=rc.reg_of(v);
	if(!it.empty()&&rc.is_valid(v))
		return it;
	load(dest,v);
	return dest;
}
static string write_val(koopa_raw_value_t v,const string &src)
{
	auto it=rc.reg_of(v);
	if(it.empty())
		_sw(src,"sp",addr.query(v));
	else
		rc.mark_valid(v);
	return it.empty()?"":it;
}
void visit(const koopa_raw_load_t &i,const koopa_raw_value_t &value)
{
	auto p=global_table.find(i.src);
	auto it=rc.reg_of(value);
	string r=it.empty()?"t0":it;
	if(p!=global_table.end())
	{
		_la("t1",p->second);
		_lw(r,"t1",0);
	}
	else
		if(ptr_value.count(i.src))
		{
			string ptr=read_val(i.src,"t1");
			_lw(r,ptr,0);
		}
		else
			_lw(r,"sp",addr.query(i.src));
	if(r=="t0")
		_sw("t0","sp",addr.query(value));
	else
		rc.mark_valid(value);
	if(ptr2_value.count(i.src))
		ptr_value.count(value);
}
void visit(const koopa_raw_store_t &i)
{
	string v=read_val(i.value,"t1");
	auto p=global_table.find(i.dest);
	if(p!=global_table.end())
	{
		_la("t0",p->second);
		_sw(v,"t0",0);
	}
	else
		if(ptr_value.count(i.dest))
		{
			string ptr=read_val(i.dest,"t0");
			_sw(v,ptr,0);
		}
		else
		{
			_sw(v,"sp",addr.query(i.dest));
			auto it=rc.reg_of(i.dest);
			if(!it.empty()&&rc.is_valid(i.dest))
				rc.mark_valid(i.dest);
		}
}
void visit(const koopa_raw_get_ptr_t &i,const koopa_raw_value_t &value)
{
	auto it=rc.reg_of(value);
	string r=it.empty()?"t0":it;
	string src=read_val(i.src,it.empty()?"t0":it);
	if(r!=src)
		_mv(r,src);
	string idx=read_val(i.index,(r=="t0")?"t1":"t0");
	_add(idx,idx,idx);
	_add(idx,idx,idx);
	_add(r,r,idx);
	write_val(value,r);
	ptr_value.insert(value);
}
void visit(const koopa_raw_get_elem_ptr_t &i,const koopa_raw_value_t &value)
{
	auto it=rc.reg_of(value);
	string r=it.empty()?"t0":it;
	if(ptr_value.count(i.src))
	{
		string src=read_val(i.src,it.empty()?"t0":it);
		if(r!=src)
			_mv(r,src);
	}
	else
	{
		_clear_peep();
		load_addr(r,i.src);
	}
	string idx=read_val(i.index,(r=="t0")?"t1":"t0");
	_add(idx,idx,idx);
	_add(idx,idx,idx);
	_add(r,r,idx);
	write_val(value,r);
	ptr_value.insert(value);
}
void visit(const koopa_raw_binary_t &i,const koopa_raw_value_t &value)
{
	auto reg=rc.reg_of(value);
	bool spilled=reg.empty();
	string res=spilled?"t0":reg;
	if(i.lhs->kind.tag==KOOPA_RVT_INTEGER)
		_li(res,i.lhs->kind.data.integer.value);
	else
	{
		auto rl=rc.reg_of(i.lhs);
		if(!rl.empty()&&rc.is_valid(i.lhs))
			_mv(res,rl);
		else
			load(res,i.lhs);
	}
	string rhs;
	if(i.rhs->kind.tag==KOOPA_RVT_INTEGER)
	{
		rhs=(res=="t0")?"t1":"t0";
		_li(rhs,i.rhs->kind.data.integer.value);
	}
	else
	{
		auto rr=rc.reg_of(i.rhs);
		if(!rr.empty()&&rc.is_valid(i.rhs))
			rhs=rr;
		else
		{
			rhs=(res=="t0")?"t1":"t0";
			load(rhs,i.rhs);
		}
	}
	switch(i.op)
	{
		case KOOPA_RBO_NOT_EQ:
			_xor(res,res,rhs);
			_snez(res,res);
			break;
		case KOOPA_RBO_EQ:
			_xor(res,res,rhs);
			_seqz(res,res);
			break;
		case KOOPA_RBO_GT:
			_sgt(res,res,rhs);
			break;
		case KOOPA_RBO_LT:
			_slt(res,res,rhs);
			break;
		case KOOPA_RBO_GE:
			_slt(res,res,rhs);
			_seqz(res,res);
			break;
		case KOOPA_RBO_LE:
			_sgt(res,res,rhs);
			_seqz(res,res);
			break;
		case KOOPA_RBO_ADD:
			_add(res,res,rhs);
			break;
		case KOOPA_RBO_SUB:
			_sub(res,res,rhs);
			break;
		case KOOPA_RBO_MUL:
			_mul(res,res,rhs);
			break;
		case KOOPA_RBO_DIV:
			_div(res,res,rhs);
			break;
		case KOOPA_RBO_MOD:
			_mod(res,res,rhs);
			break;
		case KOOPA_RBO_AND:
			_and(res,res,rhs);
			break;
		case KOOPA_RBO_OR:
			_or(res,res,rhs);
			break;
		default:
			assert(false);
	}
	if(spilled)
		_sw(res,"sp",addr.query(value));
	else
		rc.mark_valid(value);
}
void visit(const koopa_raw_branch_t &i)
{
	string cond_reg=read_val(i.cond,"t0");
	rc.flush_all();
	static int cnt=0;
	string str="long_branch"+to_string(cnt++);
	_clear_peep();
	cout<<"\tbnez "<<cond_reg<<", "<<str<<"\n";
	_clear_peep();
	cout<<"\tj "<<i.false_bb->name+1<<"\n";
	_clear_peep();
	cout<<str<<":\n";
	_clear_peep();
	cout<<"\tj "<<i.true_bb->name+1<<"\n";
}
void visit(const koopa_raw_jump_t &i)
{
	rc.flush_all();
	_clear_peep();
	cout<<"\tj "<<i.target->name+1<<"\n";
}
void visit(const koopa_raw_call_t &i,const koopa_raw_value_t &value)
{
	rc.flush_all();
	int len=i.args.len;
	for(int j=0;j<len;++j)
	{
		auto val=reinterpret_cast<koopa_raw_value_t>(i.args.buffer[j]);
		if(j<8)
			load("a"+to_string(j),val);
		else
		{
			load("t1",val);
			_sw("t1","sp",(j-8)*4);
		}
	}
	_clear_peep();
	cout<<"\tcall "<<i.callee->name+1<<"\n";
	if(value->ty->tag!=KOOPA_RTT_UNIT)
	{
		auto it=rc.reg_of(value);
		string r=it.empty()?"t0":it;
		_mv(r,"a0");
		if(r=="t0")
			_sw("t0","sp",addr.query(value));
		else
			rc.mark_valid(value);
	}
}
void visit(const koopa_raw_return_t &i)
{
	if(i.value)
	{
		if(i.value->kind.tag==KOOPA_RVT_INTEGER)
			_li("a0",i.value->kind.data.integer.value);
		else
		{
			auto reg=rc.reg_of(i.value);
			if(!reg.empty()&&rc.is_valid(i.value))
				_mv("a0",reg);
			else
				load("a0",i.value);
		}
	}
	else
		_li("a0",0);
	if(addr.R)
		_lw("ra","sp",addr.S+addr.A);
	addi("sp","sp",addr.T);
	_ret();
}
void solve_riscv(const char *str)
{
	koopa_program_t program;
	koopa_error_code_t ret = koopa_parse_from_string(str, &program);
	assert(ret == KOOPA_EC_SUCCESS);
	koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
	koopa_raw_program_t raw = koopa_build_raw_program(builder, program);
	koopa_delete_program(program);
	visit(raw);
	koopa_delete_raw_program_builder(builder);
}
