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

/* 文字列を比較。memcmpは成功すると0を返す。 */
static bool startswith(char* p1, char* p2){
    return strncmp(p1, p2, strlen(p2)) == 0;
}

/* keywordだった場合、keywordの長さを返す。それ以外の時0 */
static size_t is_keyword(char* p){
    static char* kw[] = {"return", "if", "else", "while", "for", "int", "sizeof"};
    for(size_t i =0; i < sizeof(kw) / sizeof(*kw); i++){
        if(strncmp(p, kw[i], strlen(kw[i])) == 0){
            return strlen(kw[i]);
        }
    }
    return 0;
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
            continue;
        }

        /* 2文字の予約記号 */
        /* 可変長operator。これを先に置かないと例えば<=が<と=という二つに解釈されてしまう。*/
        if(startswith(p, "==") || startswith(p, "!=") || startswith(p, "<=") || startswith(p, ">=")) {
            cur = new_token(TK_RESERVED, cur, p, 2);
            p += 2;
            continue;
        }

         /* 一文字の予約記号
        strchrは第一引数で渡された検索対象から第二引数の文字を探してあればその文字へのポインターを、なければNULLを返す。*/
        if(strchr("+-*/()<>;={},&*[]", *p)){
            cur = new_token(TK_RESERVED, cur, p, 1);
            p++;
            continue;
        }

        size_t len = is_keyword(p);
        if(len){
            cur = new_token(TK_RESERVED, cur, p, len);
            p += len;
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
            
            continue;
        }

        /* それ以外 */
        error_at(p, "トークナイズできません\n");
    }

    /* 終了を表すトークンを作成 */
    cur = new_token(TK_EOF, cur, p, 0);
    
    /* トークンの先頭へのポインタをセット */
    token = head.next;

    /* debug info */
    display_tokenizer_output(token);
}