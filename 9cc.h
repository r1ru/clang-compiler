#ifndef _9CC_H_
#define _9CC_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#define STREAM stdout
#define ERROR stderr

typedef struct Node Node;
typedef struct Member Member;

/* tokenize.c */
typedef struct Token Token;

typedef enum{
    TK_IDENT, // identifier
    TK_PUNCT, // punctuator
    TK_KEYWORD, // keyword
    TK_NUM,
    TK_STR,
    TK_EOF
}TokenKind;

struct Token{
    Token* next;
    TokenKind kind;
    uint64_t val;
    char* str;
    int len; // トークンの長さ
};

void error(char *fmt, ...);
void error_at(char *loc, char* fmt, ...);
bool is_ident(void);
bool is_str(void);
bool at_eof(void);
void next_token(void);
bool is_equal(Token *tok, char *op);
bool consume(char* op);
void expect(char* op);
uint64_t expect_number(void);
void tokenize(char *path, char* p);

/* type.c */
typedef enum{
    TY_LONG,
    TY_INT,
    TY_SHORT,
    TY_CHAR,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY,
    TY_STRUCT,
    TY_UNION,
    TY_VOID
}TypeKind;

typedef struct Type Type;
struct Type{
    TypeKind kind;
    int size;
    int align; // alignment
    Token *name;
    Type *base;

    /* array */
    int array_len;

    /* struct members */
    Member *members;

    /* function type */
    Type *ret_ty;
    Type *next;
    Type *params;
};

extern Type *ty_long;
extern Type *ty_int;
extern Type *ty_short;
extern Type *ty_char;
extern Type *ty_void;

Type *new_type(TypeKind kind, int size, int align);
Type* pointer_to(Type *base);
Type* array_of(Type *base, int len);
Type* func_type(Type *ret_ty);
Type* copy_type(Type *ty);
bool is_integer(Type *ty);
bool is_ptr(Type* ty);
bool is_void(Type *ty);
bool is_func(Type *ty);
bool is_array(Type *ty);
bool is_struct(Type *ty);
bool is_union(Type *ty);
void add_type(Node* np);

/* parse.c */
/* 構造体のメンバ情報 */
struct Member{
    Member *next;
    Type *ty;
    Token *name;
    int offset;    
};

/* グローバル変数の初期値 */
typedef struct InitData InitData;
struct InitData{
    InitData *next;
    int64_t val;
    char *label;
};

typedef struct Obj Obj;

struct Obj{
    Obj *next;
    Type *ty; // 型情報
    char *name; // 変数の名前
    int offset; // RBPからのオフセット

    // global variable
    bool is_global;
    char *str; // stirng literal or global char array
    InitData *init_data;

    //function
    bool is_definition;
    Obj *params;
    Obj *locals;
    Node *body;
    int stack_size;
};

typedef enum{
    ND_EXPR_STMT, // expression statement
    ND_STMT_EXPR, // [GNU] statement expression
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_EQ, // ==
    ND_NE, // !=
    ND_LT, // < less than
    ND_LE, // <= less than or equal to
    ND_NEG, // negative number
    ND_ASSIGN, // =
    ND_VAR, // variable
    ND_NUM,
    ND_RET, // return
    ND_IF, // if
    ND_WHILE, // while
    ND_FOR, // for
    ND_BLOCK, // {}
    ND_FUNCCALL, // function call
    ND_ADDR, // unary &
    ND_DEREF, // unary *
    ND_MEMBER, // .
    ND_CAST
}NodeKind;

struct Node{
    Node* next;
    NodeKind kind;
    Type *ty;

    Node *lhs; // left hand side
    Node *rhs; // right hand side
   
   /* if while for statement */
    Node* cond; 
    Node* then; 
    Node* els; 
    Node* init; 
    Node* inc; 

    Member *member; // 構造体のメンバ

    char* funcname; // function name
    Node* args; // argments;

    Node* body; // ND_BLOCK or ND_STMT_EXPR
    uint64_t val; // ND_NUM用
    Obj* var; // ND_VAR用
};

extern Token *token;

Obj* parse(void);

/* codegen.c */
void codegen(Obj *program);
int align_to(int offset, int align);

#endif