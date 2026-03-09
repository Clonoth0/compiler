#pragma once

#include<cstdio>
#include<memory>
#include<string>
#include<optional>
#include<iostream>
#include"koopa.h"

void visit(const koopa_raw_program_t &program);
void visit(const koopa_raw_slice_t &slice);
void visit(const koopa_raw_function_t &func);
void visit(const koopa_raw_basic_block_t &bb);
void visit(const koopa_raw_value_t &value);
void visit(const koopa_raw_return_t &i);
void visit(const koopa_raw_integer_t &i);
void visit(const koopa_raw_load_t &i,const koopa_raw_value_t &value);
void visit(const koopa_raw_store_t &i);
void visit(const koopa_raw_binary_t &i,const koopa_raw_value_t &value);
void visit(const koopa_raw_branch_t &i);
void visit(const koopa_raw_jump_t &i);
void solve_riscv(const char *str);