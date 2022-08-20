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

typedef struct Token Token;

/* 字句解析用 */
typedef enum{
    TK_RESERVED,
    TK_NUM,
    TK_EOF
}TokenKind;

struct Token{
    TokenKind kind;
    Token* next;
    int val;
    char* str;
    int len; /* トークンの長さ */
};

typedef enum{
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_NUM,
    ND_EQ, // ==
    ND_NE, // !=
    ND_LT, // < less than
    ND_LE, // <= less than or equal to
}NodeKind;

typedef struct Node Node;

struct Node{
    NodeKind kind;
    Node *lhs; /* left hand side */
    Node *rhs; /* right hand side */
    int val; /* ND_NUM用 */ 
};

extern Token* token;
extern char* input;
Token* tokenize(void);
Node* expr(void);

void gen(Node* np);

#endif