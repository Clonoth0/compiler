#include<iostream>
#include<optional>
#include<cassert>
#include<string>
#include<map>
#include<unordered_map>
#include<unordered_set>
#include"include/asm.hpp"
#include"include/koopa.h"

using namespace std;

static void _word(const int &value)
{
	cout<<"\t.word "<<value<<"\n";
}
static void _ret()
{
	cout<<"\tret\n";
}
static void _add(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\tadd "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _sub(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\tsub "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _slt(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\tslt "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _sgt(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\tsgt "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _seqz(const string &rd,const string &rs)
{
	cout<<"\tseqz "<<rd<<", "<<rs<<"\n";
}
static void _snez(const string &rd,const string &rs)
{
	cout<<"\tsnez "<<rd<<", "<<rs<<"\n";
}
static void _or(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\tor "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _and(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\tand "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _xor(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\txor "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _mul(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\tmul "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _div(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\tdiv "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _mod(const string &rd,const string &rs1,const string &rs2)
{
	cout<<"\trem "<<rd<<", "<<rs1<<", "<<rs2<<"\n";
}
static void _li(const string &rd,const int &imm)
{
	cout<<"\tli "<<rd<<", "<<imm<<"\n";
}
static void _la(const string &rd,const string &lable)
{
	cout<<"\tla "<<rd<<", "<<lable<<"\n";
}
static void _mv(const string &rd,const string &rs)
{
	cout<<"\tmv "<<rd<<", "<<rs<<"\n";
}
static void _lw(const string &rs,const string &rd,const int &imm)
{
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
void visit(const koopa_raw_program_t &program)
{
	cout<<"\t.data\n";
	visit(program.values);
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
	cout<<"\t.global "<<func->name+1<<"\n";
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
	cout<<i.value;
}
void visit(const koopa_raw_global_alloc_t &i,const koopa_raw_value_t &value)
{
	string name="v"+to_string(global_total++);
	cout<<"\t.global "<<name<<"\n";
	cout<<name<<":\n";
	if(i.init->kind.tag==KOOPA_RVT_INTEGER)
		_word(i.init->kind.data.integer.value);
	else
		if(i.init->kind.tag==KOOPA_RVT_ZERO_INIT)
			cout<<"\t.zero "<<get_alloc_size(i.init->ty)<<"\n";
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
	if(p!=global_table.end())
	{
		_la("t1",p->second);
		_lw("t1","t1",0);
	}
	else
		if(ptr_value.count(i.src))
		{
			_lw("t1","sp",addr.query(i.src));
			_lw("t1","t1",0);
		}
		else
			_lw("t1","sp",addr.query(i.src));
	_sw("t1","sp",addr.query(value));
	if(ptr2_value.count(i.src))
		ptr_value.count(value);
}
void visit(const koopa_raw_store_t &i)
{
	load("t1",i.value);
	auto p=global_table.find(i.dest);
	if(p!=global_table.end())
	{
		_la("t2",p->second);
		_sw("t1","t2",0);
	}
	else
		if(ptr_value.count(i.dest))
		{
			_lw("t2","sp",addr.query(i.dest));
			_sw("t1","t2",0);
		}
		else
			_sw("t1","sp",addr.query(i.dest));
}
void visit(const koopa_raw_get_ptr_t &i,const koopa_raw_value_t &value)
{
	load("t1",i.src);
	load("t2",i.index);
	_add("t2","t2","t2");
	_add("t2","t2","t2");
	_add("t1","t1","t2");
	_sw("t1","sp",addr.query(value));
	ptr_value.insert(value);
}
void visit(const koopa_raw_get_elem_ptr_t &i,const koopa_raw_value_t &value)
{
	load_addr("t1",i.src);
	load("t2",i.index);
	_add("t2","t2","t2");
	_add("t2","t2","t2");
	_add("t1","t1","t2");
	_sw("t1","sp",addr.query(value));
	ptr_value.insert(value);
}
void visit(const koopa_raw_binary_t &i,const koopa_raw_value_t &value)
{
	load("t1",i.lhs);
	load("t2",i.rhs);
	const string now="t1",lhs="t1",rhs="t2";
	switch(i.op)
	{
		case KOOPA_RBO_NOT_EQ:
			_xor(now,lhs,rhs);
			_snez(now,now);
			break;
		case KOOPA_RBO_EQ:
			_xor(now,lhs,rhs);
			_seqz(now,now);
			break;
		case KOOPA_RBO_GT:
			_sgt(now,lhs,rhs);
			break;
		case KOOPA_RBO_LT:
			_slt(now,lhs,rhs);
			break;
		case KOOPA_RBO_GE:
			_slt(now,lhs,rhs);
			_seqz(now,now);
			break;
		case KOOPA_RBO_LE:
			_sgt(now,lhs,rhs);
			_seqz(now,now);
			break;
		case KOOPA_RBO_ADD:
			_add(now,lhs,rhs);
			break;
		case KOOPA_RBO_SUB:
			_sub(now,lhs,rhs);
			break;
		case KOOPA_RBO_MUL:
			_mul(now,lhs,rhs);
			break;
		case KOOPA_RBO_DIV:
			_div(now,lhs,rhs);
			break;
		case KOOPA_RBO_MOD:
			_mod(now,lhs,rhs);
			break;
		case KOOPA_RBO_AND:
			_and(now,lhs,rhs);
			break;
		case KOOPA_RBO_OR:
			_or(now,lhs,rhs);
			break;
		default:
			assert(false);
	}
	_sw(now,"sp",addr.query(value));
}
void visit(const koopa_raw_branch_t &i)
{
	load("t1",i.cond);
	cout<<"\tbnez t1, "<<i.true_bb->name+1<<"\n";
	cout<<"\tj "<<i.false_bb->name+1<<"\n";
}
void visit(const koopa_raw_jump_t &i)
{
	cout<<"\tj "<<i.target->name+1<<"\n";
}
void visit(const koopa_raw_call_t &i,const koopa_raw_value_t &value)
{
	int len=i.args.len;
	for(int j=0;j<len;++j)
	{
		auto value=reinterpret_cast<koopa_raw_value_t>(i.args.buffer[j]);
		if(j<8)
			load("a"+to_string(j),value);
		else
		{
			load("t1",value);
			_sw("t1","sp",(j-8)*4);
		}
	}
	cout<<"\tcall "<<i.callee->name+1<<"\n";
	if(value->ty->tag!=KOOPA_RTT_UNIT)
		_sw("a0","sp",addr.query(value));
}
void visit(const koopa_raw_return_t &i)
{
	if(i.value)
		load("a0",i.value);
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
	assert(ret == KOOPA_EC_SUCCESS);  // 确保解析时没有出错
	// 创建一个 raw program builder, 用来构建 raw program
	koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
	// 将 Koopa IR 程序转换为 raw program
	koopa_raw_program_t raw = koopa_build_raw_program(builder, program);
	// 释放 Koopa IR 程序占用的内存
	koopa_delete_program(program);
	// 处理 raw program
	// ...
	visit(raw);
	// 处理完成, 释放 raw program builder 占用的内存
	// 注意, raw program 中所有的指针指向的内存均为 raw program builder 的内存
	// 所以不要在 raw program 处理完毕之前释放 builder
	koopa_delete_raw_program_builder(builder);
}
