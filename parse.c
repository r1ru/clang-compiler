#include "9cc.h"

Token *token;

static Vector *locals;
static Vector *globals;

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

/* トークンの名前をバッファに格納してポインタを返す。strndupと同じ動作。 */
static char* get_ident(Token* tp){
    char* name = calloc(1, tp -> len + 1); // null終端するため。
    return strncpy(name, tp -> str, tp -> len);
}

static bool is_ident(void){
    return token -> kind == TK_IDENT;
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
static Obj *find_var(Token* tp) {
    int i;
    for(i = 0; i < locals -> len; i++){
        Obj *lvar = locals -> data[i];
        if(strlen(lvar-> name) == tp -> len && !strncmp(lvar -> name, tp -> str, tp -> len)){
            next_token();
            return lvar;
        }
    }
    for(i = 0; i < globals -> len; i++){
        Obj *gvar = globals -> data[i];
        if(strlen(gvar-> name) == tp -> len && !strncmp(gvar -> name, tp -> str, tp -> len)){
            next_token();
            return gvar;
        }
    }
    return NULL;
}

/* 新しい変数を作成 */
static Obj* new_var(char* name, Type* ty){
    Obj* var = calloc(1, sizeof(Obj));
    var -> ty = ty;
    var -> name = name;
    return var;
}

/* 新しい変数を作成して引数で指定されたVectorに格納。 TODO: 重複定義を落とす*/
static Obj *new_lvar(char* name, Type *ty){
    Obj *lvar = new_var(name, ty);
    vec_push(locals, lvar);
    return lvar;
}

static Obj *new_gvar(char *name, Type *ty) {
    Obj *gvar = new_var(name, ty);
    gvar -> is_global = true;
    vec_push(globals, gvar);
    return gvar;
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
static void declaration(void); //今はまだ初期化式がないのでvoid
static Node* expr(void);
static Node* assign(void);
static Node* equality(void);
static Node * relational(void);
static Node* add(void);
static Node* mul(void);
static Node* unary(void);
static Node* postfix(void);
static Node* primary(void);
static Node* funcall(void);

/* program  = type-specifier declarator ";"
            | type-specifier declarator body */
Vector * parse(void){
    globals = new_vec();
    while(!at_eof()){
        Type *base = type_specifier();
        Type *ty = declarator(base);
        if(is_func(ty)){
            function(ty);
        }
        else{
            new_gvar(get_ident(ty -> name), ty);
            expect(";");
        }
    }
    return globals;
}

static void create_param_lvars(Vector* params){
    /* 引数が無ければ返る。*/
    if(!params){
        return;
    }
    for(int i = 0; i < params -> len; i++){
        Type *param = params -> data[i];
        new_lvar(get_ident(param -> name), param);
    }
}

/* function = "{" compound_stmt */
static void function(Type *ty){
    locals = new_vec();
    create_param_lvars(ty -> params);
    Obj* func = new_gvar(get_ident(ty -> name), ty);
    func -> num_params = locals -> len;
    expect("{");
    func -> body = compound_stmt();
    func-> locals = locals;
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
    np = new_node(ND_EXPR_STMT);
    np -> rhs = expr();
    expect(";");
    return np;
}

static bool is_typename(void){
    return is_equal(token, "int") || is_equal(token, "char");
}

/* compound-stmt = (declaration | stmt)* "}" */
static Node* compound_stmt(void){
    Node *np = new_node(ND_BLOCK);
    np -> body = new_vec();
    while(!consume("}")){
        if(is_typename()){
            declaration();
        }
        else{
            Node *s = stmt();
            add_type(s); // 型チェック
            vec_push(np -> body, s); 
        }
    }
    return np;
}

static Type* type_specifier(void){
    if(consume("int")){
        return ty_int;
    }
    if(consume("char")){
        return ty_char;
    }
    error("unknown type");
}

/* func-params = (param ("," param)*)? ")"
 param       = type-specifier declarator */
static Type* func_params(Type *ret_ty){
    Type *func = func_type(ret_ty);
    if(!is_equal(token, ")")){
        func -> params = new_vec();
        do{
            Type *base = type_specifier();
            Type *ty = declarator(base);
            vec_push(func -> params, copy_type(ty)); // copyしないと上書きされる可能性があるから。
        }while(consume(","));   
    }
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
        int size = expect_number();
        expect("]"); 
        return array_of(ty, size);
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

/* declaration = type-specifier declarator ";" TODO: 重複定義を落とす*/
static void declaration(void){
    Type* base = type_specifier();
    Type* ty = declarator(base);
    new_lvar(get_ident(ty -> name), ty);
    expect(";");
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

static Node* new_unary(NodeKind kind, Node *rhs){
    Node* np = new_node(kind);
    np -> rhs = rhs;
    return np;
}

static Node* new_add(Node *lhs, Node *rhs){
    add_type(lhs);
    add_type(rhs);
    /* pointer + pointerはエラー */
    if(is_ptr(lhs -> ty) && is_ptr(rhs -> ty)){
        error("error: invalid operand");
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
        error("error: invalid operand");
    }
    /* pointer - pointerは要素数(どちらの型も同じことが期待されている。) */
    if(is_ptr(lhs -> ty) && is_ptr(rhs -> ty)){
        lhs = new_binary(ND_SUB, lhs, rhs);
        return new_binary(ND_MUL, lhs, new_num_node(lhs -> ty -> base -> size));
    }
    /* pointer - num は pointer - sizeof(type) * num */
    if(is_ptr(lhs -> ty) && is_integer(rhs -> ty)){
        rhs = new_binary(ND_MUL, new_num_node(lhs -> ty -> base -> size), rhs);
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
            | funcall
            | "(" expr ")" */
static Node* primary(void){
    Node* np;

    /* "("ならexprを呼ぶ */
    if(consume("(")){
        np = expr();
        expect(")");
        return np;
    }

    if(is_ident()){
        if(is_equal(token -> next, "(")){
            return funcall();
        }
        Obj *var = find_var(token);
        if(!var){
            error_at(token -> str, "undefined variable");
        }
        return new_var_node(var);
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
    /* 引数がある場合 */
    if(!is_equal(token, ")")){
        np -> args = new_vec();
        do{
            vec_push(np->args, expr());
        }while(consume(","));
    }
    expect(")");
    return np;   
}