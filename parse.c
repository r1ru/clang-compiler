#include "9cc.h"

Token *token;

static Function* current_fp; // 現在parseしている関数へのポインタ。

/* 次のトークンを読む。(tokenを更新するのはこの関数のみ) */
static void next_token(void){
    token = token -> next;
}

/* トークンの記号が期待したもののときtrue。それ以外の時false */
static bool is_equal(char* op){
    if(strlen(op) != token -> len || strncmp(op, token -> str, token -> len))
        return false;
    return true;
}

/* トークンが期待した記号のときはトークンを読み進めて真を返す。それ以外のときは偽を返す。*/
static bool consume(char* op){
    if(is_equal(op)){
        next_token();
        return true;
    }
    return false;
}

/* トークンの名前をバッファに格納してポインタを返す。strndupと同じ動作。 */
static char* get_ident(Token* tp){
    char* name = calloc(1, tp -> len + 1); // null終端するため。
    return strncpy(name, tp -> str, tp -> len);
}

/* TK_IDENT用。トークンが識別子のときはトークンを読み進めてトークンへのポインタを返す。それ以外のときはNULLを返す。*/
static Token* consume_ident(void){
    if(token -> kind != TK_IDENT) {
       return NULL;
    }
    Token* tp = token;
    next_token();
    return tp;
}

/* トークンが期待した記号の時はトークンを読み進めて真を返す。それ以外の時にエラー */
static void expect(char* op){
    if(strlen(op) != token -> len || strncmp(op, token -> str, token -> len))
        error_at(token->str, "%sではありません\n", op);
    next_token();
}

/* TK_NUM用。トークンが数値の時にトークンを読み進めて数値を返す。それ以外の時エラー */
static int expect_number(void){
    if(token -> kind != TK_NUM)
        error_at(token -> str, "数ではありません\n");
    int val = token -> val;
    next_token();
    return val;
}

/* TK_IDENT用。トークンが識別子の時はトークンを読み進めて名前を返す。それ以外の時エラー */
static char* expect_ident(void){
    if(token -> kind != TK_IDENT){
        error_at(token -> str, "識別子ではありません。\n");
    }
    char* name = get_ident(token);
    next_token();
    return name;
}

/* TK_EOF用。トークンがEOFかどうかを返す。*/
static bool at_eof(void){
    return token -> kind == TK_EOF;
}

/* 変数を名前で検索する。見つからなかった場合はNULLを返す。 */
static Obj *find_lvar(Token* tp) {
    for(size_t i = 0; i < current_fp -> locals -> len; i++){
        Obj* lvar = current_fp -> locals -> data[i];
        if(strlen(lvar-> name) == tp -> len && !strncmp(lvar -> name, tp -> str, tp -> len)){
            return lvar;
        }
    }
    return NULL;
}

/* 新しい変数を作成 */
static Obj* new_lvar(char* name){
    Obj* lvar = calloc(1, sizeof(Obj));
    lvar -> name = name;
    current_fp -> stacksiz += 8;
    lvar -> offset = current_fp -> stacksiz; // TOOD: ここをもう少し分かりやすく。
    return lvar;
}

/* 新しい関数を作成 */
static Function* new_func(char* name){
    Function* fp = calloc(1, sizeof(Function));
    fp -> name = name;
    fp -> locals = new_vec();
    return fp;
}

/* 新しいnodeを作成 */
static Node* new_node(NodeKind kind){
    Node* np = calloc(1, sizeof(Node));
    np -> kind = kind;
    return np;
}

/* 二項演算(binary operation)用 */
static Node* new_binary(NodeKind kind, Node* lhs, Node* rhs){
    Node* np = new_node(kind);
    np -> lhs = lhs;
    np -> rhs = rhs;
    return np;
}

/* ND_NUMを作成 */
static Node* new_num_node(int val){
    Node* np = new_node(ND_NUM);
    np -> val = val;
    return np;
}

/* ND_LVARを作成 */
static Node* new_lvar_node(Token* tp){
    Node* np = new_node(ND_LVAR);

    /* ローカル変数が既に登録されているか検索 */
    Obj* lvar = find_lvar(tp);
    /* されていなければエラー */
    if(!lvar){
        error("undefined variable %*.s", tp -> len, tp -> str);
    }
    np -> var = lvar; 
    return np;
}

Function* function(void);
static Node* stmt(void);
static Vector* compound_stmt(void);
static Node* expr(void);
static Node* assign(void);
static Node* equality(void);
static Node * relational(void);
static Node* add(void);
static Node* mul(void);
static Node* unary(void);
static Node* primary(void);
static Node* funcall(Token* tp);

/* program = function-definition* */
void parse(void){
    Function head = {};
    Function* cur = &head;
    while(!at_eof()){
        cur = cur -> next = function();
    }
    program = head.next;
    /* debug info */
    display_parser_output(program);
}

/* function-definition = "int" ident "(" func-params? ")" "{" compound_stmt */
Function* function(void){
    expect("int");
    char* name = expect_ident();
    Function* fp = new_func(name);
    current_fp = fp;
    expect("(");
    /* 引数がある場合 */
    if(!is_equal(")")){
        do{ 
            expect("int");
            vec_push(current_fp -> locals, new_lvar(expect_ident()));
            current_fp -> num_params++;
        }while(consume(","));
    }
    expect(")");
    expect("{");
    fp -> body = compound_stmt();
    return fp;    
}

/* stmt = expr ";" 
        | "{" compound-stmt
        | "return" expr ";" 
        | "if" "(" expr ")" stmt ("else" stmt)?
        | "while" "(" expr ")" stmt
        | "for" "(" expr? ";" expr? ";" expr? ")" stmt */
static Node* stmt(void){
    Node* np;

    if(consume("{")){
        np = new_node(ND_BLOCK);
        np -> body = compound_stmt();
        return np;
    }

    /* "return" expr ";" */
    if(consume("return")){
        np = new_node(ND_RET);
        np -> rhs = expr();
        expect(";");
        return np;
    }

    /* "if" "(" expr ")" stmt ("else" stmt)? */
    if(consume("if")){
        expect("(");
        np = new_node(ND_IF);
        np -> cond = expr();
        expect(")");
        np -> then = stmt();
        if(consume("else")){
            np -> els = stmt();
        }
        return np;
    }

    /* "while" "(" expr ")" stmt */
    if(consume("while")){
        expect("(");
        np = new_node(ND_WHILE);
        np -> cond = expr();
        expect(")");
        np -> then = stmt();
        return np;
    }

    /* "for" "(" expr? ";" expr? ";" expr? ")" stmt */
    if(consume("for")){
        expect("(");
        np = new_node(ND_FOR);
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
        np -> then = stmt();
        return np;
    }

    /* expr ";" */
    np = new_node(ND_EXPR_STMT);
    np -> rhs = expr();
    expect(";");
    return np;
}

/* compound-stmt = (declaration | stmt)* "}" */
static Vector* compound_stmt(void){
    Vector* body = new_vec();
    while(!consume("}")){
        if(is_equal("int")){
            next_token(); // とりあえず読み飛ばすだけ。
            vec_push(current_fp -> locals, new_lvar(expect_ident())); // 変数をリストに登録
            expect(";");
        }
        else{
            vec_push(body, stmt());
        }
    }
    return body;
}

/* expr = assign */
static Node* expr(void){
    return assign();
}

/* assign = equality ("=" assign)? */
static Node* assign(void){
    Node* np = equality();
    if(consume("="))
        np = new_binary(ND_ASSIGN, np, assign()); // 代入は式。
    return np;
}

/* equality = relational ("==" relational | "!=" relational)* */
static Node* equality(void){
    Node* np = relational();
    for(;;){
        if(consume("==")){
            np = new_binary(ND_EQ, np, relational());
            continue;
        }
        if(consume("!=")){
            np = new_binary(ND_NE, np, relational()); 
            continue;
        }
        return np;
    }
}

/* relational = add ("<" add | "<=" add | ">" add | ">=" add)* */
static Node* relational(void){
    Node* np = add();
    for(;;){
        if(consume("<")){
            np = new_binary(ND_LT, np, add());
            continue;
        }
        if(consume("<=")){
            np = new_binary(ND_LE, np , add());
            continue;
        }
        if(consume(">")){
            np = new_binary(ND_LT, add(), np); /* x > y は y < xと同じ。 */
            continue;
        }
        if(consume(">=")){
            np = new_binary(ND_LE, add(), np); /* x >= y は y <= xと同じ */
            continue;
        }
        return np;
    }
}

/* add = mul ("+" mul | "-" mul)* */
static Node* add(void){
    Node* np = mul();
    for(;;){
        if(consume("+")){
            np = new_binary(ND_ADD, np, mul());
            continue;
        }
        if(consume("-")){
            np = new_binary(ND_SUB, np, mul());
            continue;
        }
        return np;
    }
}

/* mul = unary ("*" unary | "/" unary)* */
static Node* mul(void){
    Node* np = unary();
    for(;;){
        if(consume("*")){
            np = new_binary(ND_MUL, np, unary());
            continue;
        }
        if(consume("/")){
            np = new_binary(ND_DIV, np, unary());
            continue;
        }
        return np;
    }
}

/* ("+" | "-")? unaryになっているのは - - xのように連続する可能性があるから。*/
/* unary    = ("+" | "-" | "&" | "*")? unary
            | primary */
static Node* unary(void){
    Node* np;
    /* +はそのまま */
    if(consume("+")){
        return unary();
    }
    /* -xは0 - xと解釈する。 */
    if(consume("-")){
        return new_binary(ND_SUB, new_num_node(0), unary());
    }
    if(consume("&")){
        np = new_node(ND_ADDR);
        np -> rhs = unary();
        return np;
    }
    if(consume("*")){
        np = new_node(ND_DEREF);
        np -> rhs = unary();
        return np;
    }
    return primary();
}

/* primary  = num 
            | ident
            | funcall
            | "(" expr ")" */
static Node* primary(void){
    Node* np;
    Token* tp;

    /* "("ならexprを呼ぶ */
    if(consume("(")){
        np = expr();
        expect(")");
        return np;
    }

    /* 識別子トークンの場合 */
    tp = consume_ident();
    if(tp){
        /* ()が続くなら関数呼び出し */
        if(consume("(")){
            return funcall(tp);
        }
        np = new_lvar_node(tp);
        return np;
    }

    /* そうでなければ数値のはず */
    return new_num_node(expect_number());
}

/* funcall = ident "(" func-args? ")" */
static Node* funcall(Token* tp){
    Node* np = new_node(ND_FUNCCALL);
    np -> funcname = get_ident(tp);

    /* 引数がある場合 */
    if(!is_equal(")")){
        np -> args = new_vec();
        do{
            vec_push(np->args, expr());
        }while(consume(","));
    }
    expect(")");
    return np;   
}