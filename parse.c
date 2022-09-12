#include "9cc.h"

Token *token;

static Obj *locals;
static Obj *globals;

typedef struct VarScope VarScope;

struct VarScope {
    VarScope *next;
    char *name;
    Obj *var;
};

typedef struct Scope Scope;

struct Scope{
    Scope *next;
    VarScope *vars;
};

static Scope *scope = &(Scope){}; //現在のスコープ

static void enter_scope(void){
    Scope *sc = calloc(1, sizeof(Scope));
    sc -> next = scope;
    scope = sc;
}

static void leave_scope(void){
    scope = scope -> next;
}

static void push_scope(char *name, Obj *var){
    VarScope *vsc = calloc(1, sizeof(VarScope));
    vsc -> name = name;
    vsc -> var = var;
    vsc -> next = scope -> vars;
    scope -> vars = vsc;
}

/* 次のトークンを読む。(tokenを更新するのはこの関数のみ) */
static void next_token(void){
    token = token -> next;
}

/* トークンの記号が期待したもののときtrue。それ以外の時false */
static bool is_equal(Token *tok, char *op){
    if(strlen(op) != tok -> len || strncmp(op, tok -> str, tok -> len))
        return false;
    return true;
}

/* トークンが期待した記号のときはトークンを読み進めて真を返す。それ以外のときは偽を返す。*/
static bool consume(char* op){
    if(is_equal(token, op)){
        next_token();
        return true;
    }
    return false;
}

static char* strndup(char* str, int len){
    char *s = calloc(1, len + 1);
    return strncpy(s, str, len);
}

/* トークンの名前をバッファに格納してポインタを返す。strndupと同じ動作。 */
static char* get_ident(Token* tp){
    char* name = calloc(1, tp -> len + 1); // null終端するため。
    return strncpy(name, tp -> str, tp -> len);
}

static bool is_ident(void){
    return token -> kind == TK_IDENT;
}

static bool is_str(void){
    return token -> kind == TK_STR;
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

/* TK_EOF用。トークンがEOFかどうかを返す。*/
static bool at_eof(void){
    return token -> kind == TK_EOF;
}

/* 変数を名前で検索する。見つからなかった場合はNULLを返す。 */
static Obj *find_var(Token* tok) {
    for(Scope *sc = scope; sc; sc = sc -> next){
        for(VarScope *vsc = sc -> vars; vsc; vsc = vsc -> next){
            if(is_equal(tok, vsc -> name)){
                return vsc -> var;
            }
        }
    }
    return NULL;
}

/* 新しい変数を作成 */
static Obj* new_var(char* name, Type* ty){
    Obj* var = calloc(1, sizeof(Obj));
    var -> ty = ty;
    var -> name = name;
    push_scope(name, var);
    return var;
}

/* 新しい変数を作成してリストに登録。 TODO: 重複定義を落とす*/
static Obj *new_lvar(char* name, Type *ty){
    Obj *lvar = new_var(name, ty);
    lvar -> next = locals;
    locals = lvar;
    return lvar;
}

static Obj *new_gvar(char *name, Type *ty) {
    Obj *gvar = new_var(name, ty);
    gvar -> is_global = true;
    gvar -> next = globals;
    globals = gvar;
    return gvar;
}

static char* new_unique_name(void){
    static int idx;
    char *buf = calloc(1, 10);
    sprintf(buf, ".LC%d", idx);
    idx++;
    return buf;
}

static Obj *new_string_literal(Token *tok){
    Type *ty = array_of(ty_char, tok -> len + 1); // null文字があるので+1
    Obj *strl = new_gvar(new_unique_name(), ty);
    strl -> str = strndup(tok -> str, tok -> len);
    return strl;
}

/* 新しいnodeを作成 */
static Node *new_node(NodeKind kind){
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

/* 単項演算(unary operator)用 */
static Node* new_unary(NodeKind kind, Node *rhs){
    Node* np = new_node(kind);
    np -> rhs = rhs;
    return np;
}

/* ND_NUMを作成 */
static Node* new_num_node(int val){
    Node* np = new_node(ND_NUM);
    np -> val = val;
    return np;
}

/* ND_VARを作成 */
static Node* new_var_node(Obj *var){
    Node* np = new_node(ND_VAR);
    np -> var = var; 
    return np;
}

static Type* type_specifier(void);
static Type* func_params(Type *ret_ty);
static Type* type_suffix(Type *ty);
static Type* declarator(Type *ty);
static void function(Type *ty);
static Node* stmt(void);
static Node* compound_stmt(void);
static Node* declaration(void);
static Node* expr(void);
static Node* assign(void);
static Node* equality(void);
static Node * relational(void);
static Node* new_add(Node *lhs, Node *rhs);
static Node* new_sub(Node *lhs, Node *rhs);
static Node* add(void);
static Node* mul(void);
static Node* unary(void);
static Node* postfix(void);
static Node* primary(void);
static Node* funcall(void);

static int eval(Node *node, char **label){
    add_type(node);
    switch(node -> kind){
        case ND_ADD:
            return eval(node -> lhs, label) + eval(node -> rhs, label);
        case ND_SUB:
            return eval(node -> lhs, label) - eval(node -> rhs, label);
        case ND_MUL:
            return eval(node -> lhs, label) * eval(node -> rhs, label);
        case ND_DIV:
            return eval(node -> lhs, label) + eval(node -> rhs, label);
        case ND_NUM:
            return node -> val;
        case ND_ADDR:
            if(!node -> rhs -> var -> is_global){
                error("not a compile-time constant");
            }
            *label = node -> rhs -> var -> name;
            return 0;
        case ND_VAR:
            if(node -> ty -> kind != TY_ARRAY){
                error("not a compile-time constant");
            }
            *label = node -> var -> name;
            return 0;
    }
    error("initializer element is not constant");
}

static InitData* new_init_data(void){
    InitData *data = calloc(1, sizeof(InitData));
    return data;
}

static void gen_gvar_init(Obj *gvar, Node *init){
    /* 初期化式の評価結果の単方向リスト */
    InitData head = {};
    InitData *cur = &head;
    char *label = NULL;
    int val;
    for(Node *expr = init; expr; expr = expr -> next){
        cur = cur -> next = new_init_data();
        val = eval(expr, &label);
        if(label){
            cur -> label = label;
        }
        cur -> val = val;
    }
    gvar -> init_data = head.next;
}

static void skip_excess_elements(void){
    while(!is_equal(token, "}")){
        next_token();
    }
}

static void gvar_initializer(Obj *gvar){
    /* 初期式の単方向リスト */
    Node head = {};
    Node *cur = &head;
    if(is_array(gvar -> ty)){
        if(is_str()){
            if(gvar -> ty -> array_len == 0){
                gvar -> ty -> array_len = gvar -> ty -> size = token -> len + 1; // NULL文字分+1
            }
            if(gvar -> ty -> array_len < token -> len){
                gvar -> str = strndup(token -> str, gvar -> ty -> array_len);
            }else{
                gvar -> str = strndup(token -> str, token -> len);
            }
            next_token();
            return;
        }
        if(consume("{")){
            int idx = 0;
            while(!consume("}")){
                cur = cur -> next = expr();
                idx++;

                if(is_equal(token, ",")){
                    next_token();
                }
                if(gvar -> ty -> array_len == idx){
                    skip_excess_elements();
                }
            }
            /* 要素数が指定されていない場合サイズを修正する。*/
            if(gvar -> ty -> array_len == 0){
                gvar -> ty -> array_len = idx;
                gvar -> ty -> size = gvar -> ty -> base -> size * idx;
            }
        }
        else{
            error("invalid initializer"); // 配列の初期化式が不正
        }
    }else{
        cur = cur -> next = expr();
    }
    gen_gvar_init(gvar, head.next);
}

/* program  = type-specifier declarator ";"
            | type-specifier declarator body */
Obj * parse(void){
    globals = NULL;
    while(!at_eof()){
        Type *base = type_specifier();
        Type *ty = declarator(base);
        if(is_func(ty)){
            function(ty);
        }
        else{
            Obj *gvar = new_gvar(get_ident(ty -> name), ty);
            while(!is_equal(token, ";")){
                if(consume("=")){
                    gvar_initializer(gvar);
                }
                if(!consume(",")){
                    break;
                }
                ty = declarator(base);
                gvar = new_gvar(get_ident(ty -> name), ty);
            }
            expect(";");
        }
    }
    return globals;
}

/* ty->paramsは arg1->arg2->arg3 ...のようになっている。これを素直に前からnew_lvarを読んでいくと、localsは arg3->arg2->arg1という風になる。関数の先頭では渡されたパラメータを退避する必要があり、そのためにはlocalsをarg1->arg2->arg3のようにしたい。そこでty->paramsの最後の要素から生成している。*/
static void create_param_lvars(Type* param){
    if(param){
        create_param_lvars(param -> next);
        new_lvar(get_ident(param -> name), param);
    }
}

/* function = "{" compound_stmt */
static void function(Type *ty){
    locals = NULL;
    Obj* func = new_gvar(get_ident(ty -> name), ty);
    enter_scope(); //仮引数を関数のスコープに入れるため。
    create_param_lvars(ty -> params);
    func -> params = locals;
    expect("{");
    func -> body = compound_stmt();
    func-> locals = locals;
    leave_scope();
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
        return compound_stmt();
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
        if(!is_equal(token, ";")){
            np -> init = expr();
        }
        expect(";");
        if(!is_equal(token, ";")){
            np -> cond = expr();
        }
        expect(";");
        if(!is_equal(token, ")")){
            np -> inc = expr();
        }
        expect(")");
        np -> then = stmt();
        return np;
    }

    /* expr ";" */
    np = new_unary(ND_EXPR_STMT, expr());
    expect(";");
    return np;
}

static bool is_typename(void){
    return is_equal(token, "int") || is_equal(token, "char");
}

/* compound-stmt = (declaration | stmt)* "}" */
static Node* compound_stmt(void){
    Node *node = new_node(ND_BLOCK);
    Node head = {};
    Node *cur = &head;
    enter_scope();
    while(!consume("}")){
        if(is_typename()){
            cur = cur -> next = declaration();
        }
        else{
            cur = cur -> next = stmt();
        }
        add_type(cur);
    }
    leave_scope();
    node -> body = head.next;
    return node;
}

static Type* type_specifier(void){
    if(consume("int")){
        return ty_int;
    }
    if(consume("char")){
        return ty_char;
    }
    error_at(token -> str, "unknown type");
}

/* func-params = (param ("," param)*)? ")"
 param       = type-specifier declarator */
static Type* func_params(Type *ret_ty){
    Type head = {};
    Type *cur = &head;
    Type *func = func_type(ret_ty);
    if(!is_equal(token, ")")){
        do{
            Type *base = type_specifier();
            Type *ty = declarator(base);
            cur = cur -> next = copy_type(ty); // copyしないと上書きされる可能性があるから。
        }while(consume(","));   
    }
    func -> params = head.next;
    expect(")");
    return func;
}

/* type-suffix  = "(" func-params 
                | "[" num "]"
                | ε */ 
static Type* type_suffix(Type *ty){
    if(consume("(")){
        return func_params(ty);
    }
    if(consume("[")){
        int array_len;
        if(is_equal(token, "]")){
            array_len = 0; // 要素数が指定されていない場合
        }else{
            array_len = expect_number();
        }
        expect("]"); 
        return array_of(ty, array_len);
    }
    return ty;
}

/* declarator = "*"* ident type-suffix */
static Type* declarator(Type *ty){
    while(consume("*")){
        ty = pointer_to(ty);
    }
    if(token -> kind != TK_IDENT){
        error_at(token -> str, "variable name expected\n");
    }
    Token *name = token; //一時保存
    next_token();
    ty = type_suffix(ty);
    ty -> name = name;
}

static Node *gen_lvar_init(Obj *lvar, Node* init){
    /* 初期化式の単方向リスト */
    Node head = {};
    Node *cur = &head;
    if(is_array((lvar -> ty))){
        int idx = 0;
        for(Node *rhs = init; rhs; rhs = rhs -> next){
            Node *lhs =  new_unary(ND_DEREF, new_add(new_var_node(lvar), new_num_node(idx)));
            Node *node = new_binary(ND_ASSIGN, lhs, rhs);
            cur = cur -> next = new_unary(ND_EXPR_STMT, node);
            idx++;
            if(idx == lvar -> ty -> array_len){
                break;
            }
        }
    }else{
        Node *node = new_binary(ND_ASSIGN, new_var_node(lvar), init); 
        cur = cur -> next = new_unary(ND_EXPR_STMT, node);
    }
    return head.next;
}

static Node *lvar_initializer(Obj *lvar){
    /* 初期化式の右辺の単方向リスト */
    Node head = {};
    Node *cur = &head;

    if(is_array(lvar -> ty)){
        int idx = 0;
        if(is_str()){
            if(lvar -> ty -> array_len == 0){
                lvar -> ty -> array_len = token ->len + 1;
            }
            for(idx = 0; idx < token -> len && idx < lvar -> ty -> array_len; idx++){
                cur = cur -> next = new_num_node(token -> str[idx]); 
            }
            next_token();
        }else if(consume("{")){
            while(!consume("}")){
                cur = cur -> next = expr();
                idx++;

                if(is_equal(token, ",")){
                    next_token();
                }
                if(lvar -> ty -> array_len == idx){
                    skip_excess_elements();
                }
            }
            /* 要素数が指定されていない場合サイズを修正する。*/
            if(lvar -> ty -> array_len == 0){
                lvar -> ty -> array_len = idx;
                lvar -> ty -> size = lvar -> ty -> base -> size * idx;
            }
        }else{
            error("invalid initializer"); // 配列の初期化式が不正
        }
        /* 初期か式の数が要素数よりも少ないときは、残りを0クリア。*/
        if(idx < lvar -> ty -> array_len){
            for(;idx != lvar -> ty -> array_len; idx++){
                cur = cur -> next = new_num_node(0);
            }
        }
    }else{
        cur = cur -> next = expr();
    }
    return gen_lvar_init(lvar, head.next);
}

/* type-specifier declarator ("=" expr)? ("," declarator ("=" expr)?)* ";" */
static Node *declaration(void){
    Type* base = type_specifier();
    Node head = {};
    Node *cur = &head;
    while(!consume(";")){
        Type* ty = declarator(base);
        Obj *lvar = new_lvar(get_ident(ty -> name), ty);

        if(consume("=")){
           cur = cur -> next = lvar_initializer(lvar);
        }
        if(consume(",")){
            continue;
        }
    }
    Node *node = new_node(ND_BLOCK);
    node -> body = head.next;
    return node;
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

static Node* new_add(Node *lhs, Node *rhs){
    add_type(lhs);
    add_type(rhs);
    /* pointer + pointerはエラー */
    if(is_ptr(lhs -> ty) && is_ptr(rhs -> ty)){
        error("error: invalid operand"); // TODO ここを改良
    }
    /* num + pointer を pointer + num に変更　*/
    if(is_integer(lhs -> ty) && is_ptr(rhs -> ty)){
        Node* tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }
    /* pointer + num は pointer + sizeof(type) * numに変更 */
    if(is_ptr(lhs -> ty) && is_integer(rhs -> ty)){
        rhs = new_binary(ND_MUL, new_num_node(lhs -> ty -> base -> size), rhs);
    }
    return new_binary(ND_ADD, lhs, rhs);
}

static Node* new_sub(Node *lhs, Node *rhs){
    add_type(lhs);
    add_type(rhs);
    /* num - pointerはエラー */
    if(is_integer(lhs -> ty) && is_ptr(rhs -> ty)){
        error("error: invalid operand");// TODO ここを改良
    }
    /* pointer - pointerは要素数(どちらの型も同じことが期待されている。) */
    if(is_ptr(lhs -> ty) && is_ptr(rhs -> ty)){
        Node *node = new_binary(ND_SUB, lhs, rhs);
        node -> ty = ty_long; 
        return new_binary(ND_DIV, node, new_num_node(lhs -> ty -> base -> size));
    }
    /* pointer - num は pointer - sizeof(type) * num */
    if(is_ptr(lhs -> ty) && is_integer(rhs -> ty)){
        rhs = new_binary(ND_MUL, new_num_node(lhs -> ty -> base -> size), rhs);
        add_type(rhs);
    }
    return new_binary(ND_SUB, lhs, rhs);
}

/* add = mul ("+" mul | "-" mul)* */
static Node* add(void){
    Node* np = mul();
    for(;;){
        if(consume("+")){
            np = new_add(np, mul());
            continue;
        }
        if(consume("-")){
            np = new_sub(np, mul());
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
/* unary    = ("+" | "-" | "&" | "*" | "sizeof" )? unary
            | postfix */
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
        return new_unary(ND_ADDR, unary());
    }
    if(consume("*")){
        return new_unary(ND_DEREF, unary());
    }
    if(consume("sizeof")){
        np = unary();
        add_type(np);
        return new_num_node(np -> ty -> size);
    }
    return postfix();
}

/* postfix = primary ("[" expr "]")? */
static Node* postfix(void){
    Node *np = primary();
    if(consume("[")){
        Node *idx = expr();
        np = new_unary(ND_DEREF, new_add(np, idx));
        expect("]");
    }
    return np;
}

/* primary  = num
            | ident
            | str
            | funcall
            | "(" expr ")" 
            | "(" "{" compound-stmt ")" */
static Node* primary(void){
    Node* np;

    if(consume("(")){
        if(consume("{")){
            np = new_node(ND_STMT_EXPR);
            np -> body = compound_stmt() -> body;
            expect(")");
            return np; 
        }else{
            np = expr();
            expect(")");
            return np;
        }
    }

    if(is_ident()){
        if(is_equal(token -> next, "(")){
            return funcall();
        }
        Obj *var = find_var(token);
        if(!var){
            error_at(token -> str, "undefined variable");
        }
        next_token();
        return new_var_node(var);
    }

    if(is_str()){
        Obj * str = new_string_literal(token);
        next_token();
        return new_var_node(str);
    }

    /* そうでなければ数値のはず */
    return new_num_node(expect_number());
}

/* funcall = ident "(" func-args? ")" */
static Node* funcall(void){
    Node* np = new_node(ND_FUNCCALL);
    np -> funcname = get_ident(token);
    next_token();

    expect("(");

    /* 例えばf(1,2,3)の場合、リストは3->2->1のようにする。これはコード生成を簡単にするため。 */
    if(!is_equal(token, ")")){
        Node *cur = expr();
        while(consume(",")){
            Node *param = expr();
            param -> next = cur;
            cur = param;
        }
        np -> args = cur;
    }
    expect(")");
    return np;   
}