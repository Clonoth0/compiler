#include<iostream>
#include<optional>
#include<cassert>
#include<string>
#include<map>
#include<unordered_map>
#include<unordered_set>
#include<vector>
#include<algorithm>
#include<tuple>
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
		_li("t0",imm);
		_add("t0",rs1,"t0");
		_sw(rs2,"t0",0);
	}
}
static void addi(const string &rd,const string &rs1,const int &imm)
{
	_clear_peep();
	if(imm>=-2048&&imm<=2047)
		cout<<"\taddi "<<rd<<", "<<rs1<<", "<<imm<<"\n";
	else
	{
		_li("t0",imm);
		_add(rd,rs1,"t0");
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

static int get_alloc_size(const koopa_raw_type_t &ty)
{
	switch(ty->tag)
	{
		case KOOPA_RTT_UNIT: return 0;
		case KOOPA_RTT_FUNCTION: return 0;
		case KOOPA_RTT_INT32: return 4;
		case KOOPA_RTT_POINTER: return 4;
		case KOOPA_RTT_ARRAY: return ty->data.array.len*get_alloc_size(ty->data.array.base);
	}
	assert(false);
	return 0;
}

static bool is_allocatable(const koopa_raw_value_t &v)
{
	switch(v->kind.tag)
	{
		case KOOPA_RVT_LOAD: case KOOPA_RVT_GET_PTR:
		case KOOPA_RVT_GET_ELEM_PTR: case KOOPA_RVT_BINARY:
			return true;
		case KOOPA_RVT_CALL: return v->ty->tag!=KOOPA_RTT_UNIT;
		case KOOPA_RVT_FUNC_ARG_REF: return true;
		default: return false;
	}
}

struct LSInterval{koopa_raw_value_t val;int def;int last_use;};

static unordered_map<koopa_raw_value_t,int>lsra_def,lsra_last_use;
static unordered_map<koopa_raw_value_t,int>lsra_alloc;

static void lsra_record_use(koopa_raw_value_t v,int idx)
{
	if(!v||v->kind.tag==KOOPA_RVT_INTEGER||v->kind.tag==KOOPA_RVT_GLOBAL_ALLOC||global_table.count(v)) return;
	if(idx>lsra_last_use[v]) lsra_last_use[v]=idx;
}

static void run_lsra()
{
	vector<LSInterval>iv;
	for(auto &p:lsra_def){auto v=p.first;int d=p.second,e=d;auto it=lsra_last_use.find(v);if(it!=lsra_last_use.end())e=it->second;iv.push_back({v,d,e});}
	sort(iv.begin(),iv.end(),[](const LSInterval&a,const LSInterval&b){if(a.def!=b.def)return a.def<b.def;return a.last_use<b.last_use;});
	vector<tuple<int,koopa_raw_value_t,int>>act;
	vector<bool>free_r(6,true);
	vector<string>rpool={"t6","t5","t4","t3","t2","t1"};
	for(auto&x:iv)
	{
		auto v=x.val;int d=x.def,e=x.last_use;
		auto it=act.begin();
		while(it!=act.end()){if(get<2>(*it)<d){free_r[get<0>(*it)]=true;it=act.erase(it);}else ++it;}
		if((int)act.size()<6){int ri=-1;for(int i=0;i<6;++i)if(free_r[i]){ri=i;break;}free_r[ri]=false;lsra_alloc[v]=ri;auto p=act.begin();while(p!=act.end()&&get<2>(*p)<e)++p;act.insert(p,{ri,v,e});}
		else{int fe=get<2>(act.back());if(fe>e){int ri=get<0>(act.back());lsra_alloc.erase(get<1>(act.back()));lsra_alloc[v]=ri;act.pop_back();auto p=act.begin();while(p!=act.end()&&get<2>(*p)<e)++p;act.insert(p,{ri,v,e});}}
	}
}

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
		vector<string>pool;
		unordered_map<koopa_raw_value_t,string>v2r;
		unordered_map<string,koopa_raw_value_t>r2v;
		unordered_set<koopa_raw_value_t>dirty;
		koopa_raw_value_t last_get;
		unordered_map<koopa_raw_value_t,int>lsra_alloc;
		vector<string>lsra_regs={"t6","t5","t4","t3","t2","t1"};
		void _pop_or_spill()
		{
			if(pool.empty())
			{
				ensure_free(1);
			}
		}
		int _prefer_reg(koopa_raw_value_t v)
		{
			auto it=lsra_alloc.find(v);
			if(it==lsra_alloc.end()) return -1;
			return it->second;
		}
	public:
		void set_lsra(const unordered_map<koopa_raw_value_t,int>&a)
		{
			lsra_alloc=a;
		}
		void init()
		{
			v2r.clear();
			r2v.clear();
			dirty.clear();
			last_get=nullptr;
			pool={"t6","t5","t4","t3","t2","t1"};
		}
		string alloc_pool_reg()
		{
			_pop_or_spill();
			string r=pool.back();
			pool.pop_back();
			return r;
		}
		void free_pool_reg(const string &r)
		{
			pool.push_back(r);
		}
		string get(koopa_raw_value_t v)
		{
			if(v->kind.tag==KOOPA_RVT_INTEGER)
			{
				string r=alloc_pool_reg();
				load(r,v);
				return r;
			}
			auto it=v2r.find(v);
			if(it!=v2r.end())
			{
				last_get=v;
				return it->second;
			}
			int ri=_prefer_reg(v);
			if(ri>=0)
			{
				string prefer=lsra_regs[ri];
				bool in_pool=false;
				for(auto p=pool.begin();p!=pool.end();++p)
					if(*p==prefer){in_pool=true;break;}
				if(in_pool&&!r2v.count(prefer))
				{
					for(auto p=pool.begin();p!=pool.end();++p)
						if(*p==prefer){pool.erase(p);break;}
					load(prefer,v);
					v2r[v]=prefer;
					r2v[prefer]=v;
					last_get=v;
					return prefer;
				}
			}
			string r=alloc_pool_reg();
			load(r,v);
			v2r[v]=r;
			r2v[r]=v;
			last_get=v;
			return r;
		}
		void release(koopa_raw_value_t v)
		{
			auto it=v2r.find(v);
			if(it==v2r.end())
				return;
			string r=it->second;
			r2v.erase(r);
			dirty.erase(v);
			v2r.erase(it);
			pool.push_back(r);
		}
		void set(koopa_raw_value_t v,const string &r)
		{
			auto old=r2v.find(r);
			if(old!=r2v.end())
			{
				dirty.erase(old->second);
				v2r.erase(old->second);
				r2v.erase(old);
			}
			for(auto it=pool.begin();it!=pool.end();++it)
				if(*it==r){pool.erase(it);break;}
			v2r[v]=r;
			r2v[r]=v;
			dirty.insert(v);
		}
		void ensure_free(int n)
		{
			while((int)pool.size()<n)
			{
				koopa_raw_value_t v=nullptr;
				for(const auto &p:r2v)
					if(p.second!=last_get)
					{
						v=p.second;
						break;
					}
				if(!v&&!r2v.empty())
					v=r2v.begin()->second;
				if(!v)
					return;
				auto r=v2r[v];
				if(dirty.count(v))
				{
					_sw(r,"sp",addr.query(v));
					dirty.erase(v);
				}
				r2v.erase(r);
				v2r.erase(v);
				pool.push_back(r);
			}
		}
		void flush_all()
		{
			vector<pair<string,koopa_raw_value_t>>to_write;
			for(const auto &p:r2v)
				if(dirty.count(p.second))
					to_write.push_back(p);
			for(const auto &p:to_write)
				_sw(p.first,"sp",addr.query(p.second));
			v2r.clear();
			r2v.clear();
			dirty.clear();
			last_get=nullptr;
			pool={"t6","t5","t4","t3","t2","t1"};
		}
		void invalidate()
		{
			v2r.clear();
			r2v.clear();
			dirty.clear();
			last_get=nullptr;
			pool={"t6","t5","t4","t3","t2","t1"};
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

void visit(const koopa_raw_function_t &func)
{
	if(!func->bbs.len)
		return;
	_clear_peep();
	cout<<"\t.global "<<func->name+1<<"\n";
	_clear_peep();
	cout<<func->name+1<<":\n";

	lsra_def.clear();lsra_last_use.clear();lsra_alloc.clear();
	int S=0,R=0,A=0;
	for(int i=0;i<func->params.len;++i)
		lsra_def[reinterpret_cast<koopa_raw_value_t>(func->params.buffer[i])]=0;
	int idx=0;
	for(size_t i=0;i<func->bbs.len;++i)
	{
		auto bb=reinterpret_cast<koopa_raw_basic_block_t>(func->bbs.buffer[i]);
		S+=bb->insts.len*4;
		for(size_t j=0;j<bb->insts.len;++j)
		{
			auto inst=reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[j]);
			if(is_allocatable(inst)) lsra_def[inst]=idx;
			if(inst->ty->tag==KOOPA_RTT_UNIT) S-=4;
			if(inst->kind.tag==KOOPA_RVT_CALL){R=4;int len=inst->kind.data.call.args.len;if((len-8)*4>A) A=(len-8)*4;}
			if(inst->kind.tag==KOOPA_RVT_ALLOC){int size=get_alloc_size(inst->ty->data.pointer.base);S+=size-4;}
			switch(inst->kind.tag)
			{
				case KOOPA_RVT_LOAD: lsra_record_use(inst->kind.data.load.src,idx); break;
				case KOOPA_RVT_STORE: lsra_record_use(inst->kind.data.store.value,idx); if(ptr_value.count(inst->kind.data.store.dest)) lsra_record_use(inst->kind.data.store.dest,idx); break;
				case KOOPA_RVT_GET_PTR: lsra_record_use(inst->kind.data.get_ptr.src,idx); lsra_record_use(inst->kind.data.get_ptr.index,idx); break;
				case KOOPA_RVT_GET_ELEM_PTR: lsra_record_use(inst->kind.data.get_elem_ptr.src,idx); lsra_record_use(inst->kind.data.get_elem_ptr.index,idx); break;
				case KOOPA_RVT_BINARY: lsra_record_use(inst->kind.data.binary.lhs,idx); lsra_record_use(inst->kind.data.binary.rhs,idx); break;
				case KOOPA_RVT_BRANCH: lsra_record_use(inst->kind.data.branch.cond,idx); break;
				case KOOPA_RVT_CALL: for(int k=0;k<inst->kind.data.call.args.len;++k) lsra_record_use(reinterpret_cast<koopa_raw_value_t>(inst->kind.data.call.args.buffer[k]),idx); break;
				case KOOPA_RVT_RETURN: lsra_record_use(inst->kind.data.ret.value,idx); break;
				default: break;
			}
			++idx;
		}
	}
	A*=4;
	run_lsra();
	addr.init(R,S,A);
	unordered_map<koopa_raw_value_t,int>().swap(params_table);
	rc.init();
	rc.set_lsra(lsra_alloc);
	for(int i=0;i<func->params.len;++i)
	{
		auto value=reinterpret_cast<koopa_raw_value_t>(func->params.buffer[i]);
		params_table[value]=i;
	}
	addi("sp","sp",-addr.T);
	if(R)
		_sw("ra","sp",S+A);
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
void visit(const koopa_raw_load_t &i,const koopa_raw_value_t &value)
{
	auto p=global_table.find(i.src);
	string result_reg=rc.alloc_pool_reg();
	if(p!=global_table.end())
	{
		string addr_reg=rc.alloc_pool_reg();
		_la(addr_reg,p->second);
		_lw(result_reg,addr_reg,0);
		rc.free_pool_reg(addr_reg);
	}
	else
		if(ptr_value.count(i.src))
		{
			string ptr_reg=rc.get(i.src);
			_lw(result_reg,ptr_reg,0);
			if(i.src->kind.tag==KOOPA_RVT_INTEGER)
				rc.free_pool_reg(ptr_reg);
		}
		else
			_lw(result_reg,"sp",addr.query(i.src));
	rc.set(value,result_reg);
	if(ptr2_value.count(i.src))
		ptr_value.count(value);
}
void visit(const koopa_raw_store_t &i)
{
	string val_reg=rc.get(i.value);
	auto p=global_table.find(i.dest);
	if(p!=global_table.end())
	{
		string addr_reg=rc.alloc_pool_reg();
		_la(addr_reg,p->second);
		_sw(val_reg,addr_reg,0);
		rc.free_pool_reg(addr_reg);
	}
	else
		if(ptr_value.count(i.dest))
		{
			string ptr_reg=rc.get(i.dest);
			_sw(val_reg,ptr_reg,0);
			if(i.dest->kind.tag==KOOPA_RVT_INTEGER)
				rc.free_pool_reg(ptr_reg);
		}
		else
		{
			_sw(val_reg,"sp",addr.query(i.dest));
			rc.release(i.dest);
		}
	if(i.value->kind.tag==KOOPA_RVT_INTEGER)
		rc.free_pool_reg(val_reg);
}
void visit(const koopa_raw_get_ptr_t &i,const koopa_raw_value_t &value)
{
	string src_reg=rc.get(i.src);
	string idx_reg=rc.get(i.index);
	_add(idx_reg,idx_reg,idx_reg);
	_add(idx_reg,idx_reg,idx_reg);
	_add(src_reg,src_reg,idx_reg);
	if(i.index->kind.tag==KOOPA_RVT_INTEGER)
		rc.free_pool_reg(idx_reg);
	rc.release(i.src);
	rc.set(value,src_reg);
	ptr_value.insert(value);
}
void visit(const koopa_raw_get_elem_ptr_t &i,const koopa_raw_value_t &value)
{
	string addr_reg;
	if(ptr_value.count(i.src))
		addr_reg=rc.get(i.src);
	else
	{
		addr_reg=rc.alloc_pool_reg();
		load_addr(addr_reg,i.src);
	}
	string idx_reg=rc.get(i.index);
	_add(idx_reg,idx_reg,idx_reg);
	_add(idx_reg,idx_reg,idx_reg);
	_add(addr_reg,addr_reg,idx_reg);
	if(i.index->kind.tag==KOOPA_RVT_INTEGER)
		rc.free_pool_reg(idx_reg);
	if(ptr_value.count(i.src))
		rc.release(i.src);
	rc.set(value,addr_reg);
	ptr_value.insert(value);
}
void visit(const koopa_raw_binary_t &i,const koopa_raw_value_t &value)
{
	rc.ensure_free(2);
	string lhs=rc.get(i.lhs);
	string rhs=rc.get(i.rhs);
	rc.release(i.lhs);
	switch(i.op)
	{
		case KOOPA_RBO_NOT_EQ:
			_xor(lhs,lhs,rhs);
			_snez(lhs,lhs);
			break;
		case KOOPA_RBO_EQ:
			_xor(lhs,lhs,rhs);
			_seqz(lhs,lhs);
			break;
		case KOOPA_RBO_GT:
			_sgt(lhs,lhs,rhs);
			break;
		case KOOPA_RBO_LT:
			_slt(lhs,lhs,rhs);
			break;
		case KOOPA_RBO_GE:
			_slt(lhs,lhs,rhs);
			_seqz(lhs,lhs);
			break;
		case KOOPA_RBO_LE:
			_sgt(lhs,lhs,rhs);
			_seqz(lhs,lhs);
			break;
		case KOOPA_RBO_ADD:
			_add(lhs,lhs,rhs);
			break;
		case KOOPA_RBO_SUB:
			_sub(lhs,lhs,rhs);
			break;
		case KOOPA_RBO_MUL:
			_mul(lhs,lhs,rhs);
			break;
		case KOOPA_RBO_DIV:
			_div(lhs,lhs,rhs);
			break;
		case KOOPA_RBO_MOD:
			_mod(lhs,lhs,rhs);
			break;
		case KOOPA_RBO_AND:
			_and(lhs,lhs,rhs);
			break;
		case KOOPA_RBO_OR:
			_or(lhs,lhs,rhs);
			break;
		default:
			assert(false);
	}
	if(i.rhs->kind.tag==KOOPA_RVT_INTEGER)
		rc.free_pool_reg(rhs);
	rc.set(value,lhs);
}
void visit(const koopa_raw_branch_t &i)
{
	string cond_reg=rc.get(i.cond);
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
	if(i.cond->kind.tag==KOOPA_RVT_INTEGER)
		rc.free_pool_reg(cond_reg);
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
		string result_reg=rc.alloc_pool_reg();
		_mv(result_reg,"a0");
		rc.set(value,result_reg);
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
			auto reg=rc.get(i.value);
			_mv("a0",reg);
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
