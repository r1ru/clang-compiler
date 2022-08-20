#include "9cc.h"

/* 構文解析用*/
static Token *token;

/* error表示用の関数 */
char* input;

Node* code[100]; /* 文は最大で99まで */

static void error_at(char *loc, char *fmt, ...) {
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

void error(char *fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

/* 字句解析用 */

/* 新しいtokenを作成してcurにつなげる。*/
static Token* new_token(TokenKind kind, Token* cur, char* str, int len){
    Token* tp = calloc(1, sizeof(Token));
    tp -> kind = kind;
    tp -> str = str;
    tp -> len = len;
    cur -> next = tp;
    return tp;
}

/* for dubug */
static void display_token(Token *tp){

    if(tp -> kind == TK_NUM){
        fprintf(STREAM, "TK_NUM, val: %d, len: %d\n", tp -> val, tp -> len);
        return;
    }

    if(tp -> kind == TK_IDENT){
        fprintf(STREAM, "TK_IDENT, ident: %c\n", tp -> str[0]);
        return;
    }

    if(tp -> kind == TK_RESERVED){
        fprintf(STREAM, "TK_RESERVED, str: %s, len: %d\n", tp -> str, tp -> len);
        return;
    }

    if(tp -> kind == TK_EOF){
        fprintf(STREAM, "TK_EOF\n");
        return;
    }
}

/* 文字列を比較。memcmpは成功すると0を返す。 */
static bool startswith(char* p1, char* p2){
    return memcmp(p1, p2, strlen(p2)) == 0;
}

/* 入力文字列をトークナイズしてそれを返す */
void tokenize(void){
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
            cur = new_token(TK_NUM, cur, p, 0);
            char* q = p;
            cur -> val = strtol(p, &p, 10);
            cur -> len = p - q; /* 長さを記録 */
            //display_token(cur);
            continue;
        }

        /* 予約記号の場合 */
        /* 可変長operator。これを先に置かないと例えば<=が<と=という二つに解釈されてしまう。*/
        if(startswith(p, "==") || startswith(p, "!=") || startswith(p, "<=") || startswith(p, ">=")) {
            cur = new_token(TK_RESERVED, cur, p, 2);
            //display_token(cur);
            p += 2;
            continue;
        }

         /* 一文字operatorの場合。
        strchrは第一引数で渡された検索対象から第二引数の文字を探してあればその文字へのポインターを、なければNULLを返す。*/
        if(strchr("+-*/()<>;=", *p)){
            cur = new_token(TK_RESERVED, cur, p, 1);
            //display_token(cur);
            p++;
            continue;
        }

        /* 識別子の場合。(現時点では小文字1文字のみ) */
        if('a' <= *p && *p <= 'z') {
            cur = new_token(TK_IDENT, cur, p, 1);
            //display_token(cur);
            p++;
            continue;
        }

        /* それ以外 */
        error_at(p, "トークナイズできません\n");
    }

    /* 終了を表すトークンを作成 */
    cur = new_token(TK_EOF, cur, p, 0);
    //display_token(cur);
    
    /* tokenの先頭をセット */
    token = head.next;
}

/* 構文解析と意味解析用 */
/* TK_RESERVED用。トークンが期待した記号のときはトークンを読み進めて真を返す。それ以外のときは偽を返す。*/
static bool consume(char* op){
    if(token -> kind != TK_RESERVED ||strlen(op) != token -> len || memcmp(op, token -> str, token -> len))
        return false;
    token = token -> next;
    return true;
}

/* TK_IDENT用。トークンが識別子のときはトークンを読み進めて返す。識別子を第一引数に書き込む。それ以外のときは偽を返す。*/
static bool consume_ident(char* ip){
    if(token -> kind != TK_IDENT) {
       return false;
    }
    char ident = token -> str[0];
    *ip = ident;
    token = token -> next;
    return true;
}

/* TK_RESERVED用。トークンが期待した記号の時はトークンを読み進めて真を返す。それ以外の時にエラー */
static void expect(char* op){
    if(token -> kind != TK_RESERVED ||strlen(op) != token -> len || memcmp(op, token -> str, token -> len))
        error_at(token->str, "%sではありません\n", op);
    token = token -> next;
}

/* TK_NUM用。トークンが数値の時に数値を返す。それ以外の時エラー */
static int expect_number(void){
    if( token -> kind != TK_NUM)
        error_at(token->str, "数ではありません\n");
    int val = token -> val;
    token = token -> next;
    return val;
}

/* TK_EOF用。トークンがEOFかどうかを返す。*/
static bool at_eof(){
    return token -> kind == TK_EOF;
}

/* 新しいnodeを作成 */
static Node* new_node(NodeKind kind, Node* lhs, Node* rhs){
    Node* np = calloc(1, sizeof(Node));
    np -> kind = kind;
    np -> lhs = lhs;
    np -> rhs = rhs;
    return np;
}

/* ND_NUMを作成 */
static Node* new_node_num(int val){
    Node *np = calloc(1, sizeof(Node));
    np -> kind = ND_NUM;
    np -> val = val;
    return np;
}

/* ND_IDENTを作成 */
static Node* new_node_ident(int offset){
    Node *np = calloc(1, sizeof(Node));
    np -> kind = ND_LVAR;
    np ->offset = offset;
    return np;
}

void program(void);
static Node* stmt(void);
static Node* expr(void);
static Node* assign(void);
static Node* equality(void);
static Node * relational(void);
static Node* add(void);
static Node* mul(void);
static Node* unary(void);
static Node* primary(void);

/* program = stmt* */
void program(void){
    int i = 0;
    while(!at_eof()){
        code[i] = stmt();
        i++;
    }
    code[i] = NULL; /* 終了をマーク */
}

/* stmt = expr ";" */
static Node* stmt(void){
    Node* np = expr();
    expect(";");
    return np;
}

/* expr = assign */
static Node* expr(void){
    return assign();
}

/* assign = equality ("=" assign)? */
static Node* assign(void){
    Node* np = equality();
    if(consume("="))
        np = new_node(ND_ASSIGN, np, assign());
    return np;
}

/* equality = relational ("==" relational | "!=" relational)* */
static Node* equality(void){
    Node* np = relational();
    for(;;){
        if(consume("=="))
            np = new_node(ND_EQ, np, relational());
        else if(consume("!="))
            np = new_node(ND_NE, np, relational());
        else 
            return np;
    }
}

/* relational = add ("<" add | "<=" add | ">" add | ">=" add)* */
static Node* relational(void){
    Node* np = add();
    for(;;){
        if(consume("<"))
            np = new_node(ND_LT, np, add());
        else if(consume("<="))
            np = new_node(ND_LE, np , add());
        else if(consume(">"))
            np = new_node(ND_LT, add(), np); /* x > y は y < xと同じ。 */
        else if(consume(">="))
            np = new_node(ND_LE, add(), np); /* x >= y は y <= xと同じ */
        else 
            return np;
    }
}

/* add = mul ("+" mul | "-" mul)* */
static Node* add(void){
    Node* np = mul();
    for(;;){
        if(consume("+"))
            np = new_node(ND_ADD, np, mul());
        else if(consume("-"))
            np = new_node(ND_SUB, np, mul());
        else
            return np;
    }
}

/* mul = unary ("*" unary | "/" unary)* */
static Node* mul(void){
    Node* np = unary();
    for(;;){
        if(consume("*"))
            np = new_node(ND_MUL, np, unary());
        else if(consume("/"))
            np = new_node(ND_DIV, np, unary());
        else 
            return np;
    }
}

/* unary = ("+" | "-")? primary */
static Node* unary(void){
    /* +はそのまま */
    if(consume("+")){
        return primary();
    }
    /* -xは0 - xと解釈する。 */
    else if(consume("-")){
        return new_node(ND_SUB, new_node_num(0), primary());
    }
    else{
        return primary();
    }
}

/* primary = num | ident | "(" expr ")" */
static Node* primary(void){
    Node* np;
    char ident;
    /* "("ならexprを呼ぶ */
    if(consume("(")){
        np = expr();
        expect(")");
    }
    else if(consume_ident(&ident)){
        int offset = (ident - 'a' + 1) * 8; /* rbpのアドレスにはsaved rbpがあるので、+1している。 */
        np = new_node_ident(offset);
    }
    else{
        /* そうでなければ数値のはず */
        np = new_node_num(expect_number());
    }
    return np;
}