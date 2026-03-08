#pragma once

#include<memory>
#include<string>
#include<utility>
#include<optional>
#include<iostream>
#include<vector>
#include<cassert>
#include"koopa.h"
using namespace std;
enum StmtType
{
	_IF,_WHILE,_BREAK,_CONTINUE,_RETURN,_OTHER
};
class Result
{
	public:
		bool imm;
		int value;
		Result(bool _imm=false,int _value=0):imm(_imm),value(_value){}
		auto operator <=>(const Result &rhs)const=default;
};
class Symbol
{
	public:
		bool addr;
		int value;
		Symbol(bool _addr=false,int _value=0):addr(_addr),value(_value){}
};
class koopa_stream
{
	public:
		string value;
		koopa_stream &operator <<(const char *rhs)
		{
			value+=rhs;
			return *this;
		}
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
		koopa_stream &operator <<(const Symbol &rhs)
		{
			assert(rhs.addr);
			value+="@_"+to_string(rhs.value);
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
using node=unique_ptr<BaseAST>;
class CompUnitAST:public BaseAST
{
	public:
		node func_def;
		Result print()const override;
};
class FuncDefAST:public BaseAST
{
	public:
		string type;
		string ident;
		node block;
		Result print()const override;
};
class BlockAST:public BaseAST
{
	public:
		unique_ptr<vector<node>>blocks;
		Result print()const override;
};
class BlockItemAST:public BaseAST
{
	public:
		node block;
		Result print()const override;
};
class StmtAST:public BaseAST
{
	public:
		node stmt;
		Result print()const override;
};
class MatchedStmtAST:public BaseAST
{
	public:
		optional<string>lval;
		optional<node>exp,block;
		node stmt1,stmt2;
		StmtType type;
		Result print()const override;
};
class DanglingStmtAST:public BaseAST
{
	public:
		node exp;
		optional<node>matched_stmt;
		node stmt;
		Result print()const override;
};
class DeclAST:public BaseAST
{
	public:
		node decl;
		Result print()const override;
};
class ConstDeclAST:public BaseAST
{
	public:
		unique_ptr<vector<node>>defs;
		Result print()const override;
};
class ConstDefAST:public BaseAST
{
	public:
		string ident;
		node init;
		Result print()const override;
};
class ConstInitValAST:public BaseAST
{
	public:
		node exp;
		Result print()const override;
};
class ConstExpAST:public BaseAST
{
	public:
		node exp;
		Result print()const override;
};
class VarDeclAST:public BaseAST
{
	public:
		unique_ptr<vector<node>>defs;
		Result print()const override;
};
class VarDefAST:public BaseAST
{
	public:
		string ident;
		optional<node>init;
		Result print()const override;
};
class InitValAST:public BaseAST
{
	public:
		node exp;
		Result print()const override;
};



class ExpAST:public BaseAST
{
	public:
		node exp;
		Result print()const override;
};
class UnaryExpAST:public BaseAST
{
	public:
		optional<string>op;
		node exp;
		Result print()const override;
};
class PrimaryExpAST:public BaseAST
{
	public:
		node exp;
		optional<string>lval;
		optional<int>number;
		Result print()const override;
};
class MulExpAST:public BaseAST
{
	public:
		node unary_exp;
		optional<pair<node,string>>value;
		Result print()const override;
};
class AddExpAST:public BaseAST
{
	public:
		node mul_exp;
		optional<pair<node,string>>value;
		Result print()const override;
};
class RelExpAST:public BaseAST
{
	public:
		node add_exp;
		optional<pair<node,string>>value;
		Result print()const override;
};
class EqExpAST:public BaseAST
{
	public:
		node rel_exp;
		optional<pair<node,string>>value;
		Result print()const override;
};
class AndExpAST:public BaseAST
{
	public:
		node eq_exp;
		optional<pair<node,string>>value;
		Result print()const override;
};
class OrExpAST:public BaseAST
{
	public:
		node and_exp;
		optional<pair<node,string>>value;
		Result print()const override;
};