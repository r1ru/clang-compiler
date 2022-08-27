#ifndef _9CC_H_
#define _9CC_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdbool.h>

#define STREAM stdout
#define ERROR stderr

/* utils.c */
typedef struct{
    void** data; // 汎用
    unsigned int capacity;
    unsigned int len;
}Vector;

void error(char *fmt, ...);
Vector* new_vec(void);
void vec_push(Vector* vp, void* elem);

/* tokenize.c */
typedef struct Token Token;

typedef enum{
    TK_RESERVED,
    TK_IDENT, // 識別子
    TK_NUM,
    TK_EOF
}TokenKind;

struct Token{
    Token* next;
    TokenKind kind;
    int val;
    char* str;
    int len; // トークンの長さ
};

void error_at(char *loc, char *fmt, ...);
void tokenize(char* p);

/* parse.c */
typedef struct Obj Obj;

struct Obj{
    char *name; // 変数の名前
    int offset; // RBPからのオフセット
};

typedef struct Function Function;
typedef struct Node Node;

struct Function{
    Function* next;
    char* name; 
    size_t num_params; // 仮引数の数
    Vector* locals;
    Node* body;
    unsigned int stacksiz;
};

typedef enum{
    ND_EXPR_STMT, // expression statement
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_EQ, // ==
    ND_NE, // !=
    ND_LT, // < less than
    ND_LE, // <= less than or equal to
    ND_ASSIGN, // =
    ND_LVAR, // local variable
    ND_NUM,
    ND_RET, // return
    ND_IF, // if
    ND_WHILE, // while
    ND_FOR, // for
    ND_BLOCK, // {}
    ND_FUNCCALL, // function call
    ND_ADDR, // unary &
    ND_DEREF // unary *
}NodeKind;

struct Node{
    Node* next;
    NodeKind kind;
    Node *lhs; // left hand side
    Node *rhs; // right hand side
   
   /* if while for statement */
    Node* cond; 
    Node* then; 
    Node* els; 
    Node* init; 
    Node* inc; 

    char* funcname; // function name
    Vector* args; // argments;

    Node* body; // for compound statement
    int val; // ND_NUM用
    int offset; // ND_LVAL用
};

extern Token *token;
extern Function* program; 

void parse(void);

/* codegen.c */
extern Function* program;

void codegen(void);

/* debug.c */
extern FILE* debug;

void display_tokenizer_output(Token* head);
void display_parser_output(Function* head);


#endif