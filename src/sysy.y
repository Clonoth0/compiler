%code requires
{
	#include<memory>
	#include<string>
	#include"include/ast.hpp"
}

%{

#include<iostream>
#include<memory>
#include<string>
#include"include/ast.hpp"

// 声明 lexer 函数和错误处理函数
int yylex();
void yyerror(std::unique_ptr<BaseAST>&ast,const char *s);

using namespace std;

%}

%parse-param { std::unique_ptr<BaseAST>&ast }

// yylval 的定义, 我们把它定义成了一个联合体 (union)
// 因为 token 的值有的是字符串指针, 有的是整数
// 之前我们在 lexer 中用到的 str_val 和 int_val 就是在这里被定义的
// 至于为什么要用字符串指针而不直接用 string 或者 unique_ptr<string>?
// 请自行 STFW 在 union 里写一个带析构函数的类会出现什么情况
%union
{
	std::string *str_val;
	int int_val;
	BaseAST *ast_val;
}

%token INT RETURN
%token <str_val> IDENT
%token <int_val> INT_CONST

%type <str_val> FuncType
%type <ast_val> FuncDef Block Stmt
%type <int_val> Number

%%

CompUnit : FuncDef 
{
	auto comp_unit=make_unique<CompUnitAST>();
	comp_unit->func_def=unique_ptr<BaseAST>($1);
	ast=std::move(comp_unit);
};


FuncDef : FuncType IDENT '(' ')' Block 
{
	auto ast=new FuncDefAST;
	ast->type=*($1);
	ast->ident=*unique_ptr<string>($2);
	ast->block=unique_ptr<BaseAST>($5);
	$$=ast;
};

FuncType : INT
{
	$$=new string("i32");
};

Block : '{' Stmt '}'
{
	auto ast=new BlockAST;
	ast->stmt=unique_ptr<BaseAST>($2);
	$$=ast;
};

Stmt : RETURN Number ';'
{
	auto ast=new StmtAST;
	ast->number=$2;
	$$=ast;
};

Number : INT_CONST
{
	$$=$1;
};

%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(unique_ptr<BaseAST>&ast,const char *s) 
{
	cerr<<"error: "<<s<<endl;
}