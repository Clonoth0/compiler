#pragma once

#include<memory>
#include<string>
#include<utility>
#include<optional>
#include<iostream>
#include"koopa.h"
using namespace std;
class Result
{
	public:
		bool imm;
		int value;
		Result():imm(),value(){}
		Result(bool _imm,int _value):imm(_imm),value(_value){}
};
class koopa_stream
{
	public:
		string value;
		koopa_stream &operator <<(const string &rhs)
		{
			value+=rhs;
			return *this;
		}
		koopa_stream &operator <<(const Result &rhs)
		{
			if(!rhs.imm)
				value+="%";
			value+=to_string(rhs.value);
			return *this;
		} 
		const char *c_str()
		{
			return value.c_str();
		}
		operator string()const
		{
			return value;
		}
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
		unique_ptr<BaseAST>exp;
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
class MulExpAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>unary_exp;
		optional<pair<unique_ptr<BaseAST>,string>>value;
		Result print()const override;
};
class AddExpAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>mul_exp;
		optional<pair<unique_ptr<BaseAST>,string>>value;
		Result print()const override;
};
class RelExpAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>add_exp;
		optional<pair<unique_ptr<BaseAST>,string>>value;
		Result print()const override;
};
class EqExpAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>rel_exp;
		optional<pair<unique_ptr<BaseAST>,string>>value;
		Result print()const override;
};
class AndExpAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>eq_exp;
		optional<pair<unique_ptr<BaseAST>,string>>value;
		Result print()const override;
};
class OrExpAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>and_exp;
		optional<pair<unique_ptr<BaseAST>,string>>value;
		Result print()const override;
};