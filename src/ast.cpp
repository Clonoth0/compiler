#include"include/ast.hpp"

extern string mode;
extern string out;
void CompUnitAST::print()const
{
	if(mode=="-debug")
	{
		out+="CompUnit :\n{\n";
		func_def->print();
		out+="}\n";
	}
	if(mode=="-koopa"||mode=="-riscv")
		func_def->print();
}
void FuncDefAST::print()const
{
	if(mode=="-debug")
	{
		out+="FuncDef ";
		out+=type;
		out+=" ";
		out+=ident;
		out+=" :\n{\n";
		block->print();
		out+="}\n";
	}
	if(mode=="-koopa"||mode=="-riscv")
	{
		out+="fun @";
		out+=ident;
		out+="(): ";
		out+=type;
		out+=" {\n";
		block->print();
		out+="}\n";
	}
}
void BlockAST::print()const
{
	if(mode=="-debug")
	{
		out+="Block :\n{\n";
		stmt->print();
		out+="}\n";
	}
	if(mode=="-koopa"||mode=="-riscv")
	{
		out+="%entry:\n";
		stmt->print();
	}
}
void StmtAST::print()const
{
	if(mode=="-debug")
	{
		out+="Stmt : ";
		out+=number;
		out+="\n";
	}
	if(mode=="-koopa"||mode=="-riscv")
	{
		out+="\tret ";
		out+=to_string(number);
		out+="\n";
	}
}