#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdbool.h>

#define STREAM stdout
#define ERROR stderr

/* 字句解析用 */
typedef enum{
    TK_RESERVED,
    TK_NUM,
    TK_EOF
}TokenKind;

typedef struct Token Token;

struct Token{
    TokenKind kind;
    Token* next;
    int val;
    char* str;
};

/* 構文解析用*/
Token *token;

typedef enum{
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_NUM,
}NodeKind;

typedef struct Node Node;

struct Node{
    NodeKind kind;
    Node *lhs; /* left hand side */
    Node *rhs; /* right hand side */
    int val; /* ND_NUM用 */ 
};

/* error表示用の関数 */
char* input;

void error_at(char *loc, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int pos = loc - input; /* ポインタの引き算は要素数を返す。*/
  fprintf(ERROR, "%s\n", input);
  fprintf(ERROR, "%*s", pos, " "); /* pos個の空白を表示 */
  fprintf(ERROR, "^ ");
  vfprintf(ERROR, fmt, ap); 
  fprintf(ERROR, "\n");
  exit(1);
}

void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

/* 字句解析用 */

/* 新しいtokenを作成してcurにつなげる。*/
Token* new_token(TokenKind kind, Token* cur, char* str){
    Token* tp = calloc(1, sizeof(Token));
    tp -> kind = kind;
    tp -> str = str;
    cur -> next = tp;
    return tp;
}

/* for dubug */
void display(Token *tp){

    if(tp -> kind == TK_NUM){
        fprintf(STREAM, "TK_NUM, val: %d\n", tp -> val);
        return;
    }

    if(tp -> kind == TK_RESERVED){
        fprintf(STREAM, "TK_RESERVED, str: %c\n", tp -> str[0]);
        return;
    }

    if(tp -> kind == TK_EOF){
        fprintf(STREAM, "TK_EOF\n");
        return;
    }
}

/* 入力文字列をトークナイズしてそれを返す */
Token* tokenize(void){
    Token head; /* これは無駄になるがスタック領域なのでオーバーヘッドは0に等しい */
    Token* cur = &head;

    char *p = input;

    while(*p){
        /* is~関数は偽のときに0を、真の時に0以外を返す。*/
        /* spaceだった場合は無視。 */
        if(isspace(*p)){
            p++;
            continue;
        }

        /* 数値だった場合 */
        if(isdigit(*p)){
            cur = new_token(TK_NUM, cur, p);
            cur -> val = strtol(p, &p, 10);
            //display(cur);
            continue;
        }

        /* 予約記号の場合 */
        if(*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '(' || *p == ')'){
            cur = new_token(TK_RESERVED, cur, p);
            p++;
            //display(cur);
            continue;
        }

        /* それ以外 */
        error_at(p, "トークナイズできません\n");
    }

    /* 終了を表すトークンを作成 */
    cur = new_token(TK_EOF, cur, p);
    //display(cur);
    
    /* 先頭のトークンへのポインタを返す */
    return head.next;
}

/* 構文解析と意味解析用 */
/* TK_RESERVED用。トークンが期待した記号(+か-)のときはトークンを読み進めて真を返す。それ以外のときは偽を返す。*/
bool consume(char op){
    if(token -> kind != TK_RESERVED || token -> str[0] != op)
        return false;
    token = token -> next;
    return true;
}

/* TK_RESERVED用。トークンが期待した記号の時はトークンを読み進めて真を返す。それ以外の時にエラー */
void expect(char op){
    if(token -> kind != TK_RESERVED || token -> str[0] != op)
        error_at(token->str, "%cではありません\n", op);
    token = token -> next;
}

/* TK_NUM用。トークンが数値の時に数値を返す。それ以外の時エラー */
int expect_number(void){
    if( token -> kind != TK_NUM)
        error_at(token->str, "数ではありません\n");
    int val = token -> val;
    token = token -> next;
    return val;
}

/* TK_EOF用。トークンがEOFかどうかを返す。*/
bool at_eof(){
    return token -> kind == TK_EOF;
}

/* 新しいnodeを作成 */
Node* new_node(NodeKind kind, Node* lhs, Node* rhs){
    Node* np = calloc(1, sizeof(Node));
    np -> kind = kind;
    np -> lhs = lhs;
    np -> rhs = rhs;
    return np;
}

/* ND_NUMを作成 */
Node* new_node_num(int val){
    Node *np = calloc(1, sizeof(Node));
    np -> kind = ND_NUM;
    np -> val = val;
    return np;
}

Node* expr(void);
Node* mul(void);
Node* unary(void);
Node* primary(void);

/* expr = mul ("+" mul | "-" mul)* */
Node *expr(void){
    Node* np = mul();
    /* "+" mul か "-" mulを消費する */
    for(;;){
        if(consume('+'))
            np = new_node(ND_ADD, np, mul());
        else if(consume('-'))
            np = new_node(ND_SUB, np, mul());
        else 
            return np;
    }
}

/* mul = unary ("*" unary | "/" unary)* */
Node* mul(void){
    Node* np = unary();
    /* "*" unary か "/" unaryを消費する */
    for(;;){
        if(consume('*'))
            np = new_node(ND_MUL, np, unary());
        else if(consume('/'))
            np = new_node(ND_DIV, np, unary());
        else 
            return np;
    }
}

/* unary = ('+' | '-')? primary */
Node* unary(void){
    /* +はそのまま */
    if(consume('+')){
        return primary();
    }
    /* -xは0 - xと解釈する。 */
    else if(consume('-')){
        return new_node(ND_SUB, new_node_num(0), primary());
    }
    else{
        return primary();
    }
}

/* primary = num | (expr) */
Node* primary(void){
    /* '('ならexprを呼ぶ */
    if(consume('(')){
        Node* np = expr();
        expect(')');
        return np;
    }
    /* そうでなければ数値のはず */
    return new_node_num(expect_number());
}

/* コードを生成 */
void gen(Node* np){

    /* ND_KINDなら入力が一つの数値だったということ。*/
    if(np -> kind == ND_NUM){
        fprintf(STREAM, "\tpush %d\n", np -> val);
        return;
    }

    /* 左辺と右辺を計算 */
    gen(np -> lhs);
    gen(np -> rhs);

    fprintf(STREAM, "\tpop rdi\n"); //rhs
    fprintf(STREAM, "\tpop rax\n"); //lhs

    switch( np -> kind){

        case ND_ADD:
            fprintf(STREAM, "\tadd rax, rdi\n");
            break;
        
        case ND_SUB:
            fprintf(STREAM, "\tsub rax, rdi\n");
            break;

        case ND_MUL:
            fprintf(STREAM, "\timul rax, rdi\n");
            break;

        case ND_DIV:
            fprintf(STREAM, "\tcqo\n");
            fprintf(STREAM, "\tidiv rdi\n");
            break;
    }

    fprintf(STREAM, "\tpush rax\n");
}

int main(int argc, char* argv[]){
    if(argc != 2){
        fprintf(ERROR, "引数の個数が正しくありません\n");
        return EXIT_FAILURE;
    }

    /* 入力を保存 */
    input = argv[1];

    /* tokenize */
    token = tokenize();

    /* 構文解析 */
    Node *np  = expr();

     /* アセンブリの前半を出力 */
    fprintf(STREAM, ".intel_syntax noprefix\n");
    fprintf(STREAM, ".global main\n");
    fprintf(STREAM, "main:\n");

    /* コード生成 */
    gen(np);

    /* 結果をpop */
    fprintf(STREAM, "\tpop rax\n");
    fprintf(STREAM, "\tret\n");
    
    return 0;

}
