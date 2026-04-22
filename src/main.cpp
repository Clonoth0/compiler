#include<cassert>
#include<cstdio>
#include<iostream>
#include<memory>
#include<string>
#include"koopa.h"
#include"include/ast.hpp"
#include"include/asm.hpp"

using namespace std;
extern FILE *yyin;
extern int yyparse(unique_ptr<BaseAST>&ast);
string mode="-debug";
extern koopa_stream out;

int main(int argc, const char *argv[]) {
	assert(argc == 5);
	mode = argv[1];
	auto input=argv[2];
	auto output=argv[4];
	freopen(output,"w",stdout);
	yyin=fopen(input,"r");
	assert(yyin);

	unique_ptr<BaseAST>ast;
	auto ret=yyparse(ast);
	assert(!ret);
	ast->print();
	if(mode=="-koopa")
		cout<<out.c_str()<<"\n";
	if(mode=="-riscv")
	{
		cerr<<out.c_str()<<endl;
		solve_riscv(out.c_str());
	}
	return 0;
}
