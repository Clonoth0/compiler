#pragma once

#include<memory>
#include<string>
#include<iostream>
#include"koopa.h"
using namespace std;
extern string mode;
class BaseAST
{
	public:
		virtual ~BaseAST()=default;
		virtual void print()const=0;
};
class CompUnitAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>func_def;
		void print()const override;
};
class FuncDefAST:public BaseAST
{
	public:
		string type;
		string ident;
		unique_ptr<BaseAST>block;
		void print()const override;
};
class BlockAST:public BaseAST
{
	public:
		unique_ptr<BaseAST>stmt;
		void print()const override;
};
class StmtAST:public BaseAST
{
	public:
		int number;
		void print()const override;
};