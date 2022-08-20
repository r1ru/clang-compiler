#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdbool.h>

#define STREAM stdout
#define ERROR stderr

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

Token *token; /* 構文解析、意味解析に使う。 */

/* error表示用の関数 */
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
Token* tokenize(char *p){
    Token head; /* これは無駄になるがスタック領域なのでオーバーヘッドは0に等しい */
    Token* cur = &head;
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

        /* '+'か'-'だった場合 */
        if(*p == '+' || *p == '-'){
            cur = new_token(TK_RESERVED, cur, p);
            p++;
            //display(cur);
            continue;
        }

        /* それ以外 */
        error("トークナイズできません\n");
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
        error("%cではありません\n", op);
    token = token -> next;
}

/* TK_NUM用。次のトークンが数値の時に数値を返す。それ以外の時エラー */
int expect_number(void){
    if( token -> kind != TK_NUM)
        error("数ではありません\n");
    int val = token -> val;
    token = token -> next;
    return val;
}

/* TK_EOF用。トークンがEOFかどうかを返す。*/
bool at_eof(){
    return token -> kind == TK_EOF;
}

int main(int argc, char* argv[]){
    if(argc != 2){
        fprintf(ERROR, "引数の個数が正しくありません\n");
        return EXIT_FAILURE;
    }

    /* tokenize */
    token = tokenize(argv[1]);

     /* アセンブリの前半を出力 */
    fprintf(STREAM, ".intel_syntax noprefix\n");
    fprintf(STREAM, ".global main\n");
    fprintf(STREAM, "main:\n");

    /* 式の最初は数値でなければならない　*/
    fprintf(STREAM, "\tmov rax, %d\n", expect_number());

    /* EOFまで消費する */
    while(!at_eof()){
        
        if(consume('+')){
            fprintf(STREAM, "\tadd rax, %d\n", expect_number());
            continue;
        }

        /* +で無ければ-が来るはず。*/
        expect('-');
        fprintf(STREAM, "\tsub rax, %d\n", expect_number());
    }

    fprintf(STREAM, "\tret\n");
    
    return 0;

}
