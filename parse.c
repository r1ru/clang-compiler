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

static bool ischar(char c){
    return 'a' <= c && c <= 'z';
}

/* 入力文字列をトークナイズしてそれを返す */
void tokenize(void){
    Token head; /* これは無駄になるがスタック領域なのでオーバーヘッドは0に等しい */
    Token* cur = &head;
    char *q;

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
        if(strchr("+-*/()<>;=", *p)){
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

        /* ローカル変数の場合 */
        if(ischar(*p)){
            cur = new_token(TK_IDENT, cur, p, 0);
            q = p;
            while(ischar(*p)){
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
    
    /* tokenの先頭をセット */
    token = head.next;
}

/* 構文解析と意味解析用 */

typedef struct LVar LVar;

/* ローカル変数の型 */
struct LVar {
  LVar *next; // 次の変数かNULL
  char *name; // 変数の名前
  int len;    // 名前の長さ
  int offset; // RBPからのオフセット
};

/* ローカル変数の単方向リスト */
static LVar *locals;

/* 変数を名前で検索する。見つからなかった場合はNULLを返す。 */
static LVar *find_lvar(Token *tp) {
  for (LVar *lvar = locals; lvar; lvar = lvar->next){
    /* memcmpは一致したら0を返す。startswithを使ってもいいかも? */
    if (lvar -> len == tp -> len && !memcmp(lvar -> name, tp -> str, lvar -> len)){
         return lvar;   
    }
  }
  return NULL;
}

/* TK_RESERVED用。トークンが期待した記号のときは真を返す。それ以外のときは偽を返す。*/
static bool is_equal(char* op){
    if(token -> kind != TK_RESERVED ||strlen(op) != token -> len || memcmp(op, token -> str, token -> len))
        return false;
    return true;
}

/* トークンが期待した種類の記号のときはトークンを読み進めて真を返す。それ以外のときは偽を返す。*/
static bool consume(TokenKind kind, char* op){
    switch(kind){
        /* TK_RESERVEDの場合は文字列が一致するかチェック */
        case TK_RESERVED:
            if(strlen(op) != token -> len || memcmp(op, token -> str, token -> len)){
                return false;
            }
        /* それ以外の場合はkindが一致するか調べるのみ */
        default:
            if(kind != token -> kind){
                return false;
            }
        token = token -> next;
        return true;
    }
}

/* TK_IDENT用。トークンが識別子のときはトークンを読み進めてTK_IDENTへのポインタを返す。それ以外のときはNULLを返す。*/
static Token* consume_ident(void){
    if(token -> kind != TK_IDENT) {
       return NULL;
    }
    Token* tp = token;
    token = token -> next;
    return tp;
}

/* TK_RESERVED用。トークンが期待した記号の時はトークンを読み進めて真を返す。それ以外の時にエラー */
static void expect(char* op){
    if(token -> kind != TK_RESERVED ||strlen(op) != token -> len || memcmp(op, token -> str, token -> len))
        error_at(token->str, "%sではありません\n", op);
    token = token -> next;
}

/* TK_NUM用。トークンが数値の時にトークンを読み進めて数値を返す。それ以外の時エラー */
static int expect_number(void){
    if(token -> kind != TK_NUM)
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

/* ND_LVARを作成 */
static Node* new_node_lvar(Token* tp){
    Node *np;
    LVar* lvar;

    np = calloc(1, sizeof(Node));
    np -> kind = ND_LVAR;

     /* ローカル変数が既に登録されているか検索 */
    lvar = find_lvar(tp);
    /* されているならoffsetはそれを使用 */
    if(lvar){
        np -> offset = lvar -> offset; 
    }
    /* されていなければリストに登録 */
    else{
        lvar = calloc(1, sizeof(LVar));
        lvar -> next = locals;
        lvar -> name = tp -> str;
        lvar -> len = tp -> len;
        lvar -> offset = locals -> offset + 8; 
        np -> offset = lvar -> offset;
        locals = lvar;
    }
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
    locals = calloc(1, sizeof(LVar)); /* これをしないとnew_node_lvarのlocals->nextでSegmentation Faultがでる。
                                        定義のみだとlocalsはNULLなので。*/
    while(!at_eof()){
        code[i] = stmt();
        i++;
    }
    code[i] = NULL; /* 終了をマーク */
}

/* stmt = expr ";" 
        | "return" expr ";" 
        | "if" "(" expr ")" stmt ("else" stmt)?
        | "while" "(" expr ")" stmt*
        | "for" "(" expr? ";" expr? ";" expr? ")" stmt*/
static Node* stmt(void){
    Node* np;

    /* "return" expr ";" */
    if(consume(TK_RET, NULL)){
        np = calloc(1, sizeof(Node));
        np -> kind = ND_RET;
        np -> expr = expr();
        expect(";");
        return np;
    }

    /* "if" "(" expr ")" stmt ("else" stmt)? */
    if(consume(TK_IF, NULL)){
        expect("(");
        np = calloc(1, sizeof(Node));
        np -> kind = ND_IF;
        np -> cond = expr();
        expect(")");
        np -> then = stmt();
        if(consume(TK_ELSE, NULL)){
            np -> els = stmt();
        }
        return np;
    }

    /* "while" "(" expr ")" stmt */
    if(consume(TK_WHILE, NULL)){
        expect("(");
        np = calloc(1, sizeof(Node));
        np -> kind = ND_WHILE;
        np -> cond = expr();
        expect(")");
        np -> body = stmt();
        return np;
    }

    /* "for" "(" expr? ";" expr? ";" expr? ")" stmt */
    if(consume(TK_FOR, NULL)){
        expect("(");
        np = calloc(1, sizeof(Node));
        np -> kind = ND_FOR;
        if(!is_equal(";")){
            np -> init = expr();
        }
        expect(";");
        if(!is_equal(";")){
            np -> cond = expr();
        }
        expect(";");
        if(!is_equal(")")){
            np -> inc = expr();
        }
        expect(")");
        np -> body = stmt(); // bodyは必ず存在することが期待されている。
        return np;
    }

    /* expr ";" */
    np = expr();
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
    if(consume(TK_RESERVED, "="))
        np = new_node(ND_ASSIGN, np, assign()); // 代入は式。
    return np;
}

/* equality = relational ("==" relational | "!=" relational)* */
static Node* equality(void){
    Node* np = relational();
    for(;;){
        if(consume(TK_RESERVED, "=="))
            np = new_node(ND_EQ, np, relational());
        else if(consume(TK_RESERVED, "!="))
            np = new_node(ND_NE, np, relational());
        else 
            return np;
    }
}

/* relational = add ("<" add | "<=" add | ">" add | ">=" add)* */
static Node* relational(void){
    Node* np = add();
    for(;;){
        if(consume(TK_RESERVED, "<"))
            np = new_node(ND_LT, np, add());
        else if(consume(TK_RESERVED, "<="))
            np = new_node(ND_LE, np , add());
        else if(consume(TK_RESERVED, ">"))
            np = new_node(ND_LT, add(), np); /* x > y は y < xと同じ。 */
        else if(consume(TK_RESERVED, ">="))
            np = new_node(ND_LE, add(), np); /* x >= y は y <= xと同じ */
        else 
            return np;
    }
}

/* add = mul ("+" mul | "-" mul)* */
static Node* add(void){
    Node* np = mul();
    for(;;){
        if(consume(TK_RESERVED, "+"))
            np = new_node(ND_ADD, np, mul());
        else if(consume(TK_RESERVED, "-"))
            np = new_node(ND_SUB, np, mul());
        else
            return np;
    }
}

/* mul = unary ("*" unary | "/" unary)* */
static Node* mul(void){
    Node* np = unary();
    for(;;){
        if(consume(TK_RESERVED, "*"))
            np = new_node(ND_MUL, np, unary());
        else if(consume(TK_RESERVED, "/"))
            np = new_node(ND_DIV, np, unary());
        else 
            return np;
    }
}

/* unary = ("+" | "-")? primary */
static Node* unary(void){
    /* +はそのまま */
    if(consume(TK_RESERVED, "+")){
        return primary();
    }
    /* -xは0 - xと解釈する。 */
    else if(consume(TK_RESERVED, "-")){
        return new_node(ND_SUB, new_node_num(0), primary());
    }
    else{
        return primary();
    }
}

/* primary = num | ident | "(" expr ")" */
static Node* primary(void){
    Node* np;
    Token* tp;

    /* "("ならexprを呼ぶ */
    if(consume(TK_RESERVED, "(")){
        np = expr();
        expect(")");
        return np;
    }

    /* 識別子トークンの場合 */
    tp = consume_ident();
    if(tp){
        np = new_node_lvar(tp);
        return np;
    }

    /* そうでなければ数値のはず */
    return new_node_num(expect_number());
}