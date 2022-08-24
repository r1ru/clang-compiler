#include "9cc.h"

static LVar *locals; // ローカル変数の単方向リスト 
Node* code[100]; // 文は最大で99まで

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
static Node* funcall(Token* tp);

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
        | "{" stmt* "}"
        | "return" expr ";" 
        | "if" "(" expr ")" stmt ("else" stmt)?
        | "while" "(" expr ")" stmt
        | "for" "(" expr? ";" expr? ";" expr? ")" stmt */
static Node* stmt(void){
    Node* np;

    if(consume(TK_RESERVED, "{")){
        np = calloc(1, sizeof(Node));
        np -> kind = ND_BLOCK;
        np -> vec = new_vec();

        while(!consume(TK_RESERVED, "}")){
            vec_push(np -> vec, stmt());
        }
        
        return np;
    }

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
    np = calloc(1, sizeof(Node));
    np -> kind = ND_EXPR_STMT;
    np -> expr = expr();
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

/* unary = ("+" | "-")? unary | primary ( - - 10のように連続する場合があるため。)*/
static Node* unary(void){
    /* +はそのまま */
    if(consume(TK_RESERVED, "+")){
        return unary();
    }
    /* -xは0 - xと解釈する。 */
    else if(consume(TK_RESERVED, "-")){
        return new_node(ND_SUB, new_node_num(0), unary());
    }
    else{
        return primary();
    }
}

/* primary  = num 
            | ident func-args?
            | "(" expr ")" */
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
        /* ()が続くなら関数呼び出し */
        if(consume(TK_RESERVED, "(")){
            return funcall(tp);
        }
        np = new_node_lvar(tp);
        return np;
    }

    /* そうでなければ数値のはず */
    return new_node_num(expect_number());
}

/* funcall = ident "(" (assign ("," assign)*)? ")" */
static Node* funcall(Token* tp) {
    Node* np = calloc(1, sizeof(Node));
    np -> kind = ND_FUNCCALL;
    char* funcname = calloc(1, tp -> len + 1);
    np -> funcname = strncpy(funcname, tp -> str, tp -> len);

    /* 引数がある場合 */
    if(!is_equal(")")){
        np -> args = new_vec();
        do{
            vec_push(np->args, expr());
        }while(consume(TK_RESERVED, ","));
    }
    expect(")");
    return np;   
}