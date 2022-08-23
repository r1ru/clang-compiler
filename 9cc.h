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

typedef struct{
    void** data; // 汎用
    unsigned int capacity;
    unsigned int len;
}Vector;

// defined in utils.c
Vector* new_vec(void);
void vec_push(Vector* vp, void* elem);

typedef struct Token Token;

/* 字句解析用 */
typedef enum{
    TK_RESERVED,
    TK_IDENT, // 識別子
    TK_NUM,
    TK_EOF,
    TK_RET, // return
    TK_IF, // if
    TK_ELSE, // else
    TK_WHILE, // while
    TK_FOR // for
}TokenKind;

struct Token{
    TokenKind kind;
    Token* next;
    int val;
    char* str;
    int len; // トークンの長さ
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
    ND_FUNCCALL // function call
}NodeKind;

typedef struct Node Node;

struct Node{
    NodeKind kind;
    Node *lhs; // left hand side
    Node *rhs; // right hand side
    Node* expr; // "return" expr or expression statement
    /*  "if" (cond) then; "else" els;  
        "for" (init; cond; inc) body
        "while" (cond) body
    */
    Node* cond; 
    Node* then; 
    Node* els; 
    Node* init; 
    Node* inc; 
    Node* body;

    char* funcname; // function name
    Vector* args; // argments;

    Vector* vec; // for compound statement
    int val; // ND_NUM用
    int offset; // ND_LVAL用
};

extern FILE* debug;
extern char* input;
extern Node* code[100]; 

void error(char *fmt, ...);
void tokenize(void);
void program(void);

void codegen(void);

#endif