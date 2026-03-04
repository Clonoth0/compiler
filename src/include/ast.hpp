#pragma once

#include<memory>
#include<string>
#include<optional>
#include<iostream>
#include"koopa.h"
using namespace std;
class Result
{
	public:
		string value;
		Result():value(){}
		Result(string s):value(s){}
};
class BaseAST
{
	public:
		virtual ~BaseAST()=default;
		virtual Result print()const=0;
};
class CompUnitAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>func_def;
		Result print()const override;
};
class FuncDefAST:public BaseAST
{
	public:
		string type;
		string ident;
		unique_ptr<BaseAST>block;
		Result print()const override;
};
class BlockAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>stmt;
		Result print()const override;
};
class StmtAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>exp;
		Result print()const override;
};
class ExpAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>unary_exp;
		Result print()const override;
};
class UnaryExpAST:public BaseAST
{
	public:
		optional<string>op;
		unique_ptr<BaseAST>exp;
		Result print()const override;
};
class PrimaryExpAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>exp;
		optional<int>number;
		Result print()const override;
};