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
#include<vector>
#include"include/ast.hpp"

// 声明 lexer 函数和错误处理函数
int yylex();
void yyerror(node &ast,const char *s);

using namespace std;

%}

%parse-param { node &ast }

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
	vector<node> *vec_val;
}

%token INT RETURN CONST
%token <str_val> IDENT EQOP RELOP ADDOP NOTOP MULOP ANDOP OROP 
%token <int_val> INT_CONST

%type <str_val> FuncType LVal
%type <ast_val> FuncDef Decl ConstDecl ConstDef ConstInitVal VarDecl VarDef InitVal
%type <vec_val> ExConstDef ExVarDef ExBlockItem
%type <ast_val> Block BlockItem
%type <ast_val> Stmt 
%type <ast_val> Exp PrimaryExp UnaryExp MulExp AddExp RelExp EqExp AndExp OrExp ConstExp
%type <int_val> Number

%%

CompUnit : FuncDef 
{
	auto comp_unit=make_unique<CompUnitAST>();
	comp_unit->func_def=node($1);
	ast=std::move(comp_unit);
};

Decl : ConstDecl
{
	auto ast=new DeclAST;
	ast->decl=node($1);
	$$=ast;
} | VarDecl
{
	auto ast=new DeclAST;
	ast->decl=node($1);
	$$=ast;
};

ConstDecl : CONST INT ConstDef ExConstDef ';'
{
	auto ast=new ConstDeclAST;
	ast->defs=unique_ptr<vector<node>>($4);
	ast->defs->insert(ast->defs->begin(),node($3));
	$$=ast;
};

ExConstDef : 
{
	auto defs=new vector<node>;
	$$=defs;
} | ExConstDef ',' ConstDef
{
	auto defs=$1;
	defs->push_back(node($3));
	$$=defs;
};

ConstDef : IDENT '=' ConstInitVal
{
	auto ast=new ConstDefAST;
	ast->ident=*unique_ptr<string>($1);
	ast->init=node($3);
	$$=ast;
};

ConstInitVal : ConstExp
{
	auto ast=new ConstInitValAST;
	ast->exp=node($1);
	$$=ast;
};

VarDecl : INT VarDef ExVarDef ';'
{
	auto ast=new VarDeclAST;
	ast->defs=unique_ptr<vector<node>>($3);
	ast->defs->insert(ast->defs->begin(),node($2));
	$$=ast;
};

ExVarDef : 
{
	auto defs=new vector<node>;
	$$=defs;
} | ExVarDef ',' VarDef
{
	auto defs=$1;
	defs->push_back(node($3));
	$$=defs;
};

VarDef : IDENT
{
	auto ast=new VarDefAST;
	ast->ident=*unique_ptr<string>($1);
	ast->init=nullopt;
	$$=ast;
} | IDENT '=' InitVal
{
	auto ast=new VarDefAST;
	ast->ident=*unique_ptr<string>($1);
	ast->init=node($3);
	$$=ast;
};

InitVal : ConstExp
{
	auto ast=new InitValAST;
	ast->exp=node($1);
	$$=ast;
};



FuncDef : FuncType IDENT '(' ')' Block 
{
	auto ast=new FuncDefAST;
	ast->type=*unique_ptr<string>($1);
	ast->ident=*unique_ptr<string>($2);
	ast->block=node($5);
	$$=ast;
};

FuncType : INT
{
	$$=new string("i32");
};

Block : '{' ExBlockItem '}'
{
	auto ast=new BlockAST;
	ast->blocks=unique_ptr<vector<node>>($2);
	$$=ast;
};

ExBlockItem :
{
	auto blocks=new vector<node>;
	$$=blocks;
} | ExBlockItem BlockItem
{
	auto blocks=$1;
	blocks->push_back(node($2));
	$$=blocks;
};

BlockItem : Decl
{
	auto ast=new BlockItemAST;
	ast->block=node($1);
	$$=ast;
} | Stmt
{
	auto ast=new BlockItemAST;
	ast->block=node($1);
	$$=ast;
};

Stmt : LVal '=' Exp ';'
{
	auto ast=new StmtAST;
	ast->lval=*unique_ptr<string>($1);
	ast->exp=node($3);
	$$=ast;
} | RETURN Exp ';'
{
	auto ast=new StmtAST;
	ast->lval=nullopt;
	ast->exp=node($2);
	$$=ast;
};

Exp : OrExp
{
	auto ast=new ExpAST;
	ast->exp=node($1);
	$$=ast;
};

LVal : IDENT
{
	$$=$1;
};

PrimaryExp : '(' Exp ')'
{
	auto ast=new PrimaryExpAST;
	ast->exp=node($2);
	ast->lval=nullopt;
	ast->number=nullopt;
	$$=ast;
} | LVal
{
	auto ast=new PrimaryExpAST;
	ast->exp=node();
	ast->lval=*unique_ptr<string>($1);
	ast->number=nullopt;
	$$=ast;
} | Number
{
	auto ast=new PrimaryExpAST;
	ast->exp=node();
	ast->lval=nullopt;
	ast->number=$1;
	$$=ast;
};

UnaryExp : PrimaryExp
{
	auto ast=new UnaryExpAST;
	ast->op=nullopt;
	ast->exp=node($1);
	$$=ast;
} | ADDOP UnaryExp 
{
	auto ast=new UnaryExpAST;
	ast->op=*unique_ptr<string>($1);
	ast->exp=node($2);
	$$=ast;
} | NOTOP UnaryExp
{
	auto ast=new UnaryExpAST;
	ast->op=*unique_ptr<string>($1);
	ast->exp=node($2);
	$$=ast;
};

MulExp : UnaryExp
{
	auto ast=new MulExpAST;
	ast->value=nullopt;
	ast->unary_exp=node($1);
	$$=ast;
} | MulExp MULOP UnaryExp
{
	auto ast=new MulExpAST;
	ast->value=make_pair(node($1),*unique_ptr<string>($2));
	ast->unary_exp=node($3);
	$$=ast;
};

AddExp : MulExp
{
	auto ast=new AddExpAST;
	ast->value=nullopt;
	ast->mul_exp=node($1);
	$$=ast;
} | AddExp ADDOP MulExp
{
	auto ast=new AddExpAST;
	ast->value=make_pair(node($1),*unique_ptr<string>($2));
	ast->mul_exp=node($3);
	$$=ast;
};

RelExp : AddExp
{
	auto ast=new RelExpAST;
	ast->value=nullopt;
	ast->add_exp=node($1);
	$$=ast;
} | RelExp RELOP AddExp
{
	auto ast=new RelExpAST;
	ast->value=make_pair(node($1),*unique_ptr<string>($2));
	ast->add_exp=node($3);
	$$=ast;
};

EqExp : RelExp
{
	auto ast=new EqExpAST;
	ast->value=nullopt;
	ast->rel_exp=node($1);
	$$=ast;
} | EqExp EQOP RelExp
{
	auto ast=new EqExpAST;
	ast->value=make_pair(node($1),*unique_ptr<string>($2));
	ast->rel_exp=node($3);
	$$=ast;
};

AndExp : EqExp
{
	auto ast=new AndExpAST;
	ast->value=nullopt;
	ast->eq_exp=node($1);
	$$=ast;
} | AndExp ANDOP EqExp
{
	auto ast=new AndExpAST;
	ast->value=make_pair(node($1),*unique_ptr<string>($2));
	ast->eq_exp=node($3);
	$$=ast;
};

OrExp : AndExp
{
	auto ast=new OrExpAST;
	ast->value=nullopt;
	ast->and_exp=node($1);
	$$=ast;
} | OrExp OROP AndExp
{
	auto ast=new OrExpAST;
	ast->value=make_pair(node($1),*unique_ptr<string>($2));
	ast->and_exp=node($3);
	$$=ast;
};

ConstExp : Exp
{
	auto ast=new ConstExpAST;
	ast->exp=node($1);
	$$=ast;
};

Number : INT_CONST
{
	$$=$1;
};

%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(node&ast,const char *s) 
{
	cerr<<"error: "<<s<<endl;
}