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

%token VOID INT RETURN CONST IF ELSE WHILE BREAK CONTINUE
%token <str_val> IDENT EQOP RELOP ADDOP NOTOP MULOP ANDOP OROP 
%token <int_val> INT_CONST

%type <vec_val> CompUnit ExConstDef ExVarDef ExBlockItem FuncFParams FuncRParams
%type <vec_val> ExExp ExInitVal
%type <ast_val> FuncDef FuncFParam
%type <ast_val> Decl ConstDecl ConstDef VarDecl VarDef InitVal
%type <ast_val> Block BlockItem
%type <ast_val> Stmt MatchedStmt DanglingStmt
%type <ast_val> Exp LVal PrimaryExp UnaryExp MulExp AddExp RelExp EqExp AndExp OrExp
%type <int_val> Number

%%

Program : CompUnit
{
	auto program=make_unique<ProgramAST>();
	program->defs=unique_ptr<vector<node>>($1);
	ast=std::move(program);
}

CompUnit :
{
	auto defs=new vector<node>;
	$$=defs;
} | CompUnit Decl
{
	auto defs=$1;
	defs->push_back(node($2));
	$$=defs;
} | CompUnit FuncDef
{
	auto defs=$1;
	defs->push_back(node($2));
	$$=defs;
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

ConstDef : IDENT ExExp '=' InitVal
{
	auto ast=new ConstDefAST;
	ast->ident=*unique_ptr<string>($1);
	ast->exps=unique_ptr<vector<node>>($2);
	ast->init=node($4);
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

VarDef : IDENT ExExp
{
	auto ast=new VarDefAST;
	ast->ident=*unique_ptr<string>($1);
	ast->exps=unique_ptr<vector<node>>($2);
	ast->init=nullopt;
	$$=ast;
} | IDENT ExExp '=' InitVal
{
	auto ast=new VarDefAST;
	ast->ident=*unique_ptr<string>($1);
	ast->exps=unique_ptr<vector<node>>($2);
	ast->init=node($4);
	$$=ast;
};

InitVal : Exp
{
	auto ast=new InitValAST;
	ast->exp=node($1);
	ast->inits=make_unique<vector<node>>();
	$$=ast;
} | '{' '}'
{
	auto ast=new InitValAST;
	ast->exp=nullopt;
	ast->inits=make_unique<vector<node>>();
	$$=ast;
} | '{' InitVal ExInitVal '}'
{
	auto ast=new InitValAST;
	ast->exp=nullopt;
	ast->inits=unique_ptr<vector<node>>($3);
	ast->inits->insert(ast->inits->begin(),node($2));
	$$=ast;
}

ExInitVal : 
{
	auto inits=new vector<node>;
	$$=inits;
} | ExInitVal ',' InitVal
{
	auto inits=$1;
	inits->push_back(node($3));
	$$=inits;
};

FuncDef : VOID IDENT '(' ')' Block 
{
	auto ast=new FuncDefAST;
	ast->type="";
	ast->ident=*unique_ptr<string>($2);
	ast->params=make_unique<vector<node>>();
	ast->block=node($5);
	$$=ast;
} | INT IDENT '(' ')' Block 
{
	auto ast=new FuncDefAST;
	ast->type=" : i32";
	ast->ident=*unique_ptr<string>($2);
	ast->params=make_unique<vector<node>>();
	ast->block=node($5);
	$$=ast;
} | VOID IDENT '(' FuncFParam FuncFParams ')' Block 
{
	auto ast=new FuncDefAST;
	ast->type="";
	ast->ident=*unique_ptr<string>($2);
	ast->params=unique_ptr<vector<node>>($5);
	ast->params->insert(ast->params->begin(),node($4));
	ast->block=node($7);
	$$=ast;
} | INT IDENT '(' FuncFParam FuncFParams ')' Block 
{
	auto ast=new FuncDefAST;
	ast->type=" : i32";
	ast->ident=*unique_ptr<string>($2);
	ast->params=unique_ptr<vector<node>>($5);
	ast->params->insert(ast->params->begin(),node($4));
	ast->block=node($7);
	$$=ast;
};

FuncFParams : 
{
	auto params=new vector<node>;
	$$=params;
} | FuncFParams ',' FuncFParam
{
	auto params=$1;
	params->push_back(node($3));
	$$=params;
};

FuncFParam : INT IDENT
{
	auto ast=new FuncFParamAST;
	ast->ident=*unique_ptr<string>($2);
	ast->ptr=false;
	$$=ast;
} | INT IDENT '[' ']' ExExp
{
	auto ast=new FuncFParamAST;
	ast->ident=*unique_ptr<string>($2);
	ast->exps=unique_ptr<vector<node>>($5);
	ast->ptr=true;
	$$=ast;
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

Stmt : MatchedStmt
{
	auto ast=new StmtAST;
	ast->stmt=node($1);
	$$=ast;
} | DanglingStmt
{
	auto ast=new StmtAST;
	ast->stmt=node($1);
	$$=ast;
};

MatchedStmt : LVal '=' Exp ';'
{
	auto ast=new MatchedStmtAST;
	ast->lval=node($1);
	ast->exp=node($3);
	ast->block=nullopt;
	ast->type=_OTHER;
	$$=ast;
} | ';'
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=nullopt;
	ast->block=nullopt;
	ast->type=_OTHER;
	$$=ast;
} | Exp ';'
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=node($1);
	ast->block=nullopt;
	ast->type=_OTHER;
	$$=ast;
} | Block
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=nullopt;
	ast->block=node($1);
	ast->type=_OTHER;
	$$=ast;
} | RETURN ';'
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=nullopt;
	ast->block=nullopt;
	ast->type=_RETURN;
	$$=ast;
} | RETURN Exp ';'
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=node($2);
	ast->block=nullopt;
	ast->type=_RETURN;
	$$=ast;
} | IF '(' Exp ')' MatchedStmt ELSE MatchedStmt
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=node($3);
	ast->block=nullopt;
	ast->stmt1=node($5);
	ast->stmt2=node($7);
	ast->type=_IF_ELSE;
	$$=ast;
} | WHILE '(' Exp ')' MatchedStmt
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=node($3);
	ast->block=nullopt;
	ast->stmt1=node($5);
	ast->type=_WHILE;
	$$=ast;
} | BREAK ';'
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=nullopt;
	ast->block=nullopt;
	ast->type=_BREAK;
	$$=ast;
} | CONTINUE ';'
{
	auto ast=new MatchedStmtAST;
	ast->lval=nullopt;
	ast->exp=nullopt;
	ast->block=nullopt;
	ast->type=_CONTINUE;
	$$=ast;
};

DanglingStmt : IF '(' Exp ')' Stmt
{
	auto ast=new DanglingStmtAST;
	ast->exp=node($3);
	ast->stmt1=node($5);
	ast->type=_IF;
	$$=ast;
} | IF '(' Exp ')' MatchedStmt ELSE DanglingStmt
{
	auto ast=new DanglingStmtAST;
	ast->exp=node($3);
	ast->stmt1=node($5);
	ast->stmt2=node($7);
	ast->type=_IF_ELSE;
	$$=ast;
} | WHILE '(' Exp ')' DanglingStmt
{
	auto ast=new DanglingStmtAST;
	ast->exp=node($3);
	ast->stmt1=node($5);
	ast->type=_WHILE;
	$$=ast;
};

Exp : OrExp
{
	auto ast=new ExpAST;
	ast->exp=node($1);
	$$=ast;
};

LVal : IDENT ExExp
{
	auto ast=new LValAST;
	ast->ident=*unique_ptr<string>($1);
	ast->exps=unique_ptr<vector<node>>($2);
	$$=ast;
};

ExExp : 
{
	auto exps=new vector<node>;
	$$=exps;
} | ExExp '[' Exp ']'
{
	auto exps=$1;
	exps->push_back(node($3));
	$$=exps;
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
	ast->lval=node($1);
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
	ast->func=false;
	$$=ast;
} | ADDOP UnaryExp 
{
	auto ast=new UnaryExpAST;
	ast->op=*unique_ptr<string>($1);
	ast->exp=node($2);
	ast->func=false;
	$$=ast;
} | NOTOP UnaryExp
{
	auto ast=new UnaryExpAST;
	ast->op=*unique_ptr<string>($1);
	ast->exp=node($2);
	ast->func=false;
	$$=ast;
} | IDENT '(' ')'
{
	auto ast=new UnaryExpAST;
	ast->op=*unique_ptr<string>($1);
	ast->params=make_unique<vector<node>>();
	ast->func=true;
	$$=ast;
} | IDENT '(' Exp FuncRParams ')'
{
	auto ast=new UnaryExpAST;
	ast->op=*unique_ptr<string>($1);
	ast->params=unique_ptr<vector<node>>($4);
	ast->params->insert(ast->params->begin(),node($3));
	ast->func=true;
	$$=ast;
};

FuncRParams : 
{
	auto params=new vector<node>;
	$$=params;
} | FuncRParams ',' Exp
{
	auto params=$1;
	params->push_back(node($3));
	$$=params;
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