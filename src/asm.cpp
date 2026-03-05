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
static unordered_map<koopa_raw_value_t,string>reg_map;
static string new_reg()
{
	static int cnt=0;
	string res;
	if(cnt<8)
		res="t"+to_string(cnt);
	else
		if(cnt<16)
			res="a"+to_string(cnt-8);
		else
			res="s"+to_string(cnt-16);
	++cnt;
	return res;
}
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
		case KOOPA_RVT_BINARY:
			visit(kind.data.binary,value);
			break;
		default:
			assert(false);
	}
}
void visit(const koopa_raw_integer_t &i)
{
	cout<<i.value;
}
void visit(const koopa_raw_return_t &i)
{
	if(i.value)
	{
		if(i.value->kind.tag==KOOPA_RVT_INTEGER)
			_li("a0",i.value->kind.data.integer.value);
		else
			_mv("a0",reg_map[i.value]);
	}
	else
		_li("a0",0);
	_ret();
}
static bool allocate_reg(const koopa_raw_value_t &value)
{
	if(value->kind.tag==KOOPA_RVT_INTEGER)
	{
		if(!value->kind.data.integer.value)
		{
			reg_map[value]="x0";
			return false;
		}
		else
		{
			auto reg=new_reg();
			_li(reg,value->kind.data.integer.value);
			reg_map[value]=reg;
			return true;
		}
	}
	return true;
}
void visit(const koopa_raw_binary_t &i,const koopa_raw_value_t &value)
{
	bool u=allocate_reg(i.lhs),v=allocate_reg(i.rhs);
	if(u)
		reg_map[value]=reg_map[i.lhs];
	else
		if(v)
			reg_map[value]=reg_map[i.rhs];
		else
			reg_map[value]=new_reg();
	const auto now=reg_map[value];
	const auto lhs=reg_map[i.lhs];
	const auto rhs=reg_map[i.rhs];
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
