#include"include/ast.hpp"

extern string mode;
void CompUnitAST::print()const
{
	if(mode=="-debug")
	{
		cout<<"CompUnit :\n{\n";
		func_def->print();
		cout<<"}\n";
	}
	if(mode=="-koopa")
		func_def->print();
}
void FuncDefAST::print()const
{
	if(mode=="-debug")
	{
		cout<<"FuncDef "<<type<<" "<<ident<<" :\n{\n";
		block->print();
		cout<<"}\n";
	}
	if(mode=="-koopa")
	{
		cout<<"fun @"<<ident<<"(): "<<type<<" {\n";
		block->print();
		cout<<"}\n";
	}
}
void BlockAST::print()const
{
	if(mode=="-debug")
	{
		cout<<"Block :\n{\n";
		stmt->print();
		cout<<"}\n";
	}
	if(mode=="-koopa")
	{
		cout<<"%entry:\n";
		stmt->print();
	}
}
void StmtAST::print()const
{
	if(mode=="-debug")
		cout<<"Stmt : "<<number<<"\n";
	if(mode=="-koopa")
	{
		cout<<"  ret "<<number<<"\n";
	}
}