#include "9cc.h"

static char* input;

/* エラー表示用の関数 */
void error_at(char *loc, char *fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  int pos = loc - input; // ポインタの引き算は要素数を返す。
  fprintf(ERROR, "%s\n", input);
  fprintf(ERROR, "%*s", pos, " "); // pos個の空白を表示
  fprintf(ERROR, "^ ");
  vfprintf(ERROR, fmt, ap); 
  fprintf(ERROR, "\n");
  exit(1);
}

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

    switch(tp -> kind){

        case TK_RESERVED:
            fprintf(debug, "TK_RESERVED, str: %.*s\n", tp -> len, tp -> str);
            return;

        case TK_IDENT:
            fprintf(debug, "TK_IDENT, ident: %.*s\n", tp -> len , tp -> str);
            return;

        case TK_NUM:
            fprintf(debug, "TK_NUM, val: %d\n", tp -> val);
            return;

        case TK_EOF:
            fprintf(debug, "TK_EOF\n");
            return;

        case TK_RET:
            fprintf(debug, "TK_RET, str: %.*s\n", tp -> len, tp -> str);
            return;
        
        case TK_IF:
            fprintf(debug, "TK_IF, str: %.*s\n", tp -> len, tp -> str);
            return;
        
        case TK_ELSE:
            fprintf(debug, "TK_ELSE, str: %.*s\n", tp -> len, tp -> str);
            return;
        
        case TK_WHILE:
            fprintf(debug, "TK_WHILE, str: %.*s\n", tp -> len, tp -> str);
            return;

        case TK_FOR:
            fprintf(debug, "TK_FOR, str: %.*s\n", tp -> len, tp -> str);
            return;
    }
}

/* 文字列を比較。memcmpは成功すると0を返す。 */
static bool startswith(char* p1, char* p2){
    return memcmp(p1, p2, strlen(p2)) == 0;
}

/* 入力文字列をトークナイズしてそれを返す */
void tokenize(char* p){
    Token head; /* これは無駄になるがスタック領域なのでオーバーヘッドは0に等しい */
    Token* cur = &head;
    char *q;

    input = p;

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
            q = p;
            cur -> val = strtol(p, &p, 10);
            cur -> len = p - q; /* 長さを記録 */
            display_token(cur);
            continue;
        }

        /* 予約記号の場合 */
        /* 可変長operator。これを先に置かないと例えば<=が<と=という二つに解釈されてしまう。*/
        if(startswith(p, "==") || startswith(p, "!=") || startswith(p, "<=") || startswith(p, ">=")) {
            cur = new_token(TK_RESERVED, cur, p, 2);
            display_token(cur);
            p += 2;
            continue;
        }

         /* 一文字operatorの場合。
        strchrは第一引数で渡された検索対象から第二引数の文字を探してあればその文字へのポインターを、なければNULLを返す。*/
        if(strchr("+-*/()<>;={},", *p)){
            cur = new_token(TK_RESERVED, cur, p, 1);
            display_token(cur);
            p++;
            continue;
        }

         /* returnの場合(これはローカル変数よりも前に来なければならない。) */
        if(startswith(p, "return")){
            cur = new_token(TK_RET, cur, p, 6);
            display_token(cur);
            p += 6;
            continue;
        }

        /* ifの場合 */
        if(startswith(p, "if")){
            cur = new_token(TK_IF, cur, p, 2);
            display_token(cur);
            p += 2;
            continue;
        }

        /* elseの場合 */
        if(startswith(p, "else")){
            cur = new_token(TK_ELSE, cur, p, 4);
            display_token(cur);
            p += 4;
            continue;
        }

        /* whileの場合 */
        if(startswith(p, "while")){
            cur = new_token(TK_WHILE, cur, p, 5);
            display_token(cur);
            p += 5;
            continue;
        }

        /* forの場合 */
        if(startswith(p, "for")){
            cur = new_token(TK_FOR, cur, p, 3);
            display_token(cur);
            p += 3;
            continue;
        }

        /* ローカル変数の場合(数字が使用される可能性もあることに注意。) */
        if(isalnum(*p)){
            cur = new_token(TK_IDENT, cur, p, 0);
            q = p;
            while(isalnum(*p)){
                p++;
            }
            cur -> len = p - q; /* 長さを記録 */
            display_token(cur);
            
            continue;
        }

        /* それ以外 */
        error_at(p, "トークナイズできません\n");
    }

    /* 終了を表すトークンを作成 */
    cur = new_token(TK_EOF, cur, p, 0);
    //display_token(cur);
    
    /* トークンの先頭へのポインタをセット */
    token = head.next;
}