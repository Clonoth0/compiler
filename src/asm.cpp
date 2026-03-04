#include<iostream>
#include<optional>
#include<cassert>
#include"include/asm.hpp"
#include"include/koopa.h"

using namespace std;
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
		cout<<"\tli a0, ";
		cout<<i.value->kind.data.integer.value<<"\n\tret\n";
	}
	else
	{
		cout<<"\tli a0, 0\n";
		cout<<"\tret\n";
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