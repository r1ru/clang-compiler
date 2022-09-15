#include "9cc.h"

Token *token;

static Obj *locals;
static Obj *globals;

typedef struct VarScope VarScope;

struct VarScope {
    VarScope *next;
    char *name;
    Obj *var;
    Type *type_def;
};

typedef struct {
    bool is_typedef;
}VarAttr;

typedef struct TagScope TagScope;
struct TagScope{
    TagScope *next;
    char *name;
    Type *ty;
};

typedef struct Scope Scope;
/* Cには変数のスコープと構造体タグのスコープがある */
struct Scope{
    Scope *next;
    VarScope *vars;
    TagScope *tags;
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

/* 現在のScopeに名前を登録 */
static VarScope *push_scope(char *name){
    VarScope *vsc = calloc(1, sizeof(VarScope));
    vsc -> name = name;
    vsc -> next = scope -> vars;
    scope -> vars = vsc;
    return vsc;
}

/* 現在のScopeにstruct tagを登録 */
static void push_tag_scope(char *name, Type *ty){
    TagScope *tsc = calloc(1, sizeof(TagScope));
    tsc -> name = name;
    tsc -> ty = ty;
    tsc -> next = scope -> tags;
    scope -> tags = tsc;
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

/* 名前で検索する。見つからなかった場合はNULLを返す。 */
static VarScope *find_var(Token* tok) {
    for(Scope *sc = scope; sc; sc = sc -> next){
        for(VarScope *vsc = sc -> vars; vsc; vsc = vsc -> next){
            if(is_equal(tok, vsc -> name)){
                return vsc;
            }
        }
    }
    return NULL;
}

/* struct tagを名前で検索する(同じタグ名の場合新しいほうが優先される。) */
static Type* find_tag(Token *tok){
    for(Scope *sc = scope; sc; sc = sc -> next){
        for(TagScope *tsc = sc -> tags; tsc; tsc = tsc -> next){
            if(is_equal(tok, tsc -> name)){
                return tsc -> ty;
            }
        }
    }
    return NULL;
}

/* 識別子がtypedfされた型だったら型を返す。それ以外はNULLを返す */
static Type *find_typedef(){
    if(token -> kind == TK_IDENT){
        VarScope *sc = find_var(token);
        if(sc){
            return sc -> type_def; // typedefでない場合と、普通の変数の場合はどうなるのか。
        }
    } 
    return NULL;
}

/* 新しい変数を作成 */
static Obj* new_var(char* name, Type* ty){
    Obj* var = calloc(1, sizeof(Obj));
    var -> ty = ty;
    var -> name = name;
    push_scope(name) -> var = var;
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

static Type* declspec(VarAttr *attr);
static Type* func_params(Type *ret_ty);
static Type* type_suffix(Type *ty);
static Type* declarator(Type *ty);
static void function(Type *ty);
static Node* stmt(void);
static Node* compound_stmt(void);
static Node* declaration(Type *base);
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

/* 引き算の結果負の値になる可能性があるのでint64_t */
static int64_t eval(Node *node, char **label){
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
    int64_t val;
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

/*  typedefは変数定義と同じくtypedef int x, *y;のように書ける。
    chibiccにはtypedef int;のようなテストケースがあるが、役に立たないのでこういう入力は受け付けないことにする。*/
static void parse_typedef(Type *base){
    do{
        Type *ty = declarator(base);
        push_scope(get_ident(ty -> name)) -> type_def = ty;
    }while(consume(","));
    expect(";");
}

/* program = (function-definition | global-variable)* */
Obj * parse(void){
    globals = NULL;
    while(!at_eof()){
        VarAttr attr = {};
        Type *base = declspec(&attr);

        if(attr.is_typedef){
            parse_typedef(base);
            continue;
        }

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

/* function = ";" | "{" compound_stmt */
static void function(Type *ty){
    Obj* func = new_gvar(get_ident(ty -> name), ty);
    func -> is_definition = !consume(";");

    if(!func -> is_definition)
        return;
    
    locals = NULL;
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
    static char* kw[] = {"void", "char", "short", "int", "long", "void", "struct", "union", "typedef"};
    for(int i =0; i < sizeof(kw) / sizeof(*kw); i++){
        if(is_equal(token, kw[i])){
            return true;
        }
    }
    return find_typedef();
}

/* compound-stmt = (declaration | stmt)* "}" */
static Node* compound_stmt(void){
    Node *node = new_node(ND_BLOCK);
    Node head = {};
    Node *cur = &head;
    enter_scope();
    while(!consume("}")){
        if(is_typename()){
            VarAttr attr = {};
            Type *base = declspec(&attr);
            if(attr.is_typedef){
                parse_typedef(base);
                continue;
            }
            cur = cur -> next = declaration(base);
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

static Member *new_member(Token *name, Type *ty){
    struct Member *member = calloc(1, sizeof(Member));
    member -> name = name;
    member -> ty = ty;
    return member;
}


/* decl = delsepc declarator ( "," declarator)* ";" */
static Member *decl(void){
    Member head = {};
    Member *cur = &head;
    Type *base = declspec(NULL); // 構造体や共用体のメンバにtypedefはこれない
    do{
        Type *ty = declarator(base);
        cur = cur -> next = new_member(ty -> name, ty);
        if(!consume(",")){
            break;
        }
    }while(!is_equal(token, ";"));
    expect(";");
    return head.next;
}

/* struct-members = decl ("," decl )* "}" */
static Member *struct_members(void){
    Member head = {};
    Member *cur = &head;

    while(!consume("}")){
        cur = cur -> next = decl();
    }
    return head.next;
} 

/* struct-decl = ident? ("{" struct-members)? */
static Type *struct_union_decl(void){
    Token *tag = NULL;

    /* 構造体tag */
    if(is_ident()){
        tag = token;
        next_token();
    }

    /* タグ名で検索 */
    if(tag && !is_equal(token, "{")){
        Type *ty = find_tag(tag);
        if(!ty){
            error_at(tag -> str, "unknow struct type");
        }
        return ty;
    }
    expect("{");
    Type *ty = calloc(1, sizeof(Type));
    ty -> members = struct_members();
    ty -> align = 1; 

    /* tag名が指定されていた場合はリストに登録する */
    if(tag){
        push_tag_scope(get_ident(tag), ty);
    }
    return ty;
}

/* struct-decl = struct-union-decl */
static Type *struct_decl(void){
    Type *ty = struct_union_decl();
    ty -> kind = TY_STRUCT;
    int offset = 0;
    for(Member *m = ty -> members; m; m = m -> next){
        offset = align_to(offset, m -> ty -> align);
        m -> offset = offset;
        offset += m -> ty -> size;
        /* 構造体のalignmentは最もaligmenが大きいメンバに合わせる。*/
        if(ty -> align < m -> ty -> align){
            ty -> align = m -> ty -> align;
        }
    }
    ty -> size = align_to(offset, ty -> align);
    return ty;
}

/* union-decl = struct-union-decl */
static Type *union_decl(void){
    Type *ty = struct_union_decl();
    ty -> kind = TY_UNION;
    for(Member *m = ty -> members; m; m = m -> next){
        if(ty -> size < m -> ty -> size){
            ty -> size = m -> ty -> size; // 共用体のサイズは最も大きいメンバに合わせる。
        }
        if(ty -> align < m -> ty -> align){
            ty -> align = m -> ty -> align;
        }
    }
    return ty;
}

/*  declspec    = ("void" | "char" | "short" | "int" | "long" 
                | struct-decl 
                | union-decl 
                | typedef-name )+ */
static Type* declspec(VarAttr *attr){
    enum{
        VOID = 1 << 0,
        CHAR = 1 << 2,
        SHORT = 1 << 4,
        INT = 1 << 6,
        LONG = 1 << 8
    };

    int counter = 0;
    Type *ty = ty_int; // typedef tのように既存の型が指定されていない場合、intになる。

    /* counterの値を調べているのはint main(){ typedef int t; {typedef long t;} }のように同名の型が来た時に二回目のtでfind_typedef()がtrueになってしまうから。*/
    while(is_typename()){
        if(consume("typedef")){
            if(!attr){
                error_at(token -> str, "storage class specifier is not allowed in this context");
            }
            attr -> is_typedef = true;
            continue;
        }
        ty = find_typedef(token);
        /* typedefされた型だった場合 */
        if(ty && counter == 0){
            next_token();
            return ty;
        }
        /* 新しい型の宣言 */
        if(ty && counter != 0){
            break;
        }


        if(consume("struct")){
            return struct_decl();
        }
        if(consume("union")){
            return union_decl();
        }

        if(consume("void")){
            counter += VOID;
        }
        if(consume("char")){
            counter += CHAR;
        }
        if(consume("short")){
            counter += SHORT ;
        }
        if(consume("int")){
            counter += INT ;
        }
        if(consume("long")){
            counter += LONG;
        }

        switch(counter){
            case VOID:
                ty =  ty_void;
                break;

            case CHAR:
                ty = ty_char;
                break;

            case SHORT:
            case SHORT + INT:
                ty = ty_short;
                break;

            case INT:
                ty = ty_int;
                break;
            
            case LONG:
            case LONG + INT:
            case LONG + LONG:
                ty = ty_long;
                break;
            
            default:
                error_at(token -> str, "unknown type");
        }
    }
    return ty;
}

/* func-params = (param ("," param)*)? ")"
 param       = type-specifier declarator */
static Type* func_params(Type *ret_ty){
    Type head = {};
    Type *cur = &head;
    Type *func = func_type(ret_ty);
    if(!is_equal(token, ")")){
        do{
            Type *base = declspec(NULL); // 仮引数にtypedefは来れない。
            Type *ty = declarator(base);
            cur = cur -> next = copy_type(ty); // copyしないと上書きされる可能性があるから。
        }while(consume(","));   
    }
    func -> params = head.next;
    expect(")");
    return func;
}

/* type-suffix  = "(" func-params 
                | "[" num "]" type-suffix
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
        ty = type_suffix(ty);
        return array_of(ty, array_len);
    }
    return ty;
}

/* char (*a) [2];を考える。*aを読んだ段階ではこれが何のポインタなのか分からない。()がある場合は外側を先に確定させる必要がある。この例だと一旦()を無視して、int [2]を読んでint型の配列(要素数2)が確定する。次に()の中を読むことでaの型がintの配列(要素数2)へのポインタ型だと分かる。*/

/* declarator = ("*"* ident? | "(" declarator ")" ) type-suffix */
static Type* declarator(Type *ty){
    if(consume("(")){
        Token *start = token;
        Type dummy = {};
        declarator(&dummy); // とりあえず読み飛ばす
        expect(")");
        ty = type_suffix(ty); // ()の外側の型を確定させる。
        Token *end = token;
        token  = start;
        ty = declarator(ty); // ()の中の型を確定させる。
        token = end;
        return ty;
    }
    while(consume("*")){
        ty = pointer_to(ty);
    }
    if(!is_ident()){
        error_at(token -> str, "variable name expected\n");
    }
    Token *name = token; //一時保存
    next_token();
    ty = type_suffix(ty);
    ty -> name = name;
    return ty;
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

/* declspec declarator ("=" expr)? ("," declarator ("=" expr)?)* ";" */
static Node *declaration(Type *base){
    Node head = {};
    Node *cur = &head;
    while(!consume(";")){
        Type* ty = declarator(base);
        /* declaratorの後にチェックするのはvoid *p;のようにvoidへのpointer型は合法なため。*/
        if(is_void(ty)){
            error_at(ty -> name -> str, "variable declared void");
        }
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

Member *get_struct_member(Type *ty, Token *name){
    for(Member *m = ty -> members; m; m = m -> next){
        if(m -> name -> len == name -> len && !strncmp(m -> name -> str, name -> str, name -> len)){
            return m;
        }
    }
    error("%.*s: no such member", name -> len, name -> str);
}

static Node *struct_ref(Node *lhs, Token *name){
    add_type(lhs);
    if(!is_struct(lhs -> ty) && !is_union(lhs -> ty)){
        error_at(lhs -> ty -> name -> str, "not a struct nor union");
    }
    Member *member = get_struct_member(lhs -> ty, name);
    Node *node = new_node(ND_MEMBER);
    node -> lhs = lhs;
    node -> member = member;
    return node;
}

/* postfix  = primary ("[" expr "]")?
            | primary ("." ident | "->" ident)*  */
static Node* postfix(void){
    Node *np = primary();
    for(;;){
        if(consume("[")){
            Node *idx = expr();
            np = new_unary(ND_DEREF, new_add(np, idx));
            expect("]");
            continue;
        }
        if(consume(".")){
            np = struct_ref(np, token);
            next_token();
            continue;
        }
        if(consume("->")){
            np = new_unary(ND_DEREF, np);
            np = struct_ref(np, token);
            next_token();
            continue;
        }
        return np;
    }
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
        VarScope *vsc = find_var(token);
        if(!vsc || !vsc -> var){
            error_at(token -> str, "undefined variable");
        }
        next_token();
        return new_var_node(vsc -> var);
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