#include<iostream>
#include<optional>
#include<cassert>
#include<string>
#include<map>
#include<unordered_map>
#include"include/asm.hpp"
#include"include/koopa.h"

using namespace std;

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
class Address
{
	private:
		unordered_map<koopa_raw_value_t,int>f;
		int t=0;
	public:
		int cnt=0;
		int query(const koopa_raw_value_t &v)
		{
			auto p=f.find(v);
			if(p!=f.end())
				return p->second;
			f[v]=t;
			t+=4;
			return t-4;
		}
}addr;
void visit(const koopa_raw_program_t &program)
{
	visit(program.values);
	cout<<".text\n";
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
	cout<<"\t.global "<<func->name+1<<"\n";
	cout<<func->name+1<<":"<<"\n";
	int cnt=0;
	for(size_t i=0;i<func->bbs.len;++i)
	{
		auto bb=reinterpret_cast<koopa_raw_basic_block_t>(func->bbs.buffer[i]);
		cnt+=bb->insts.len;
		for(size_t j=0;j<bb->insts.len;++j)
		{
			auto inst=reinterpret_cast<koopa_raw_value_t>(bb->insts.buffer[j]);
			if(inst->ty->tag==KOOPA_RTT_UNIT)
				--cnt;
		}
	}
	cnt=(cnt*4+15)/16*16;
	addi("sp","sp",-cnt);
	addr.cnt=cnt;
	visit(func->bbs);
}
void visit(const koopa_raw_basic_block_t &bb)
{
	visit(bb->insts);
}
void visit(const koopa_raw_value_t &value)
{
	const auto &kind=value->kind;
	switch(kind.tag)
	{
		case KOOPA_RVT_RETURN:
			visit(kind.data.ret);
			break;
		case KOOPA_RVT_INTEGER:
			visit(kind.data.integer);
			break;
		case KOOPA_RVT_ALLOC:
			break;
		case KOOPA_RVT_LOAD:
			visit(kind.data.load,value);
			break;
		case KOOPA_RVT_STORE:
			visit(kind.data.store);
			break;
		case KOOPA_RVT_BINARY:
			visit(kind.data.binary,value);
			break;
		default:
			assert(false);
	}
}
void visit(const koopa_raw_return_t &i)
{
	if(i.value)
	{
		if(i.value->kind.tag==KOOPA_RVT_INTEGER)
			_li("a0",i.value->kind.data.integer.value);
		else
			_lw("a0","sp",addr.query(i.value));
	}
	else
		_li("a0",0);
	addi("sp","sp",addr.cnt);
	_ret();
}
void visit(const koopa_raw_integer_t &i)
{
	cout<<i.value;
}
void visit(const koopa_raw_load_t &i,const koopa_raw_value_t &value)
{
	_lw("t1","sp",addr.query(i.src));
	_sw("t1","sp",addr.query(value));
}
void visit(const koopa_raw_store_t &i)
{
	if(i.value->kind.tag==KOOPA_RVT_INTEGER)
		_li("t1",i.value->kind.data.integer.value);
	else
		_lw("t1","sp",addr.query(i.value));
	_sw("t1","sp",addr.query(i.dest));
}
void visit(const koopa_raw_binary_t &i,const koopa_raw_value_t &value)
{
	if(i.lhs->kind.tag==KOOPA_RVT_INTEGER)
		_li("t1",i.lhs->kind.data.integer.value);
	else
		_lw("t1","sp",addr.query(i.lhs));
	if(i.rhs->kind.tag==KOOPA_RVT_INTEGER)
		_li("t2",i.rhs->kind.data.integer.value);
	else
		_lw("t2","sp",addr.query(i.rhs));
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
