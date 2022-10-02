#include "9cc.h"

Token *token;

static Obj *locals;
static Obj *globals;

static Obj *current_fn; // 現在parseしている関数

// current_fn内のlabeled statementとgotoのリスト
static Node *labels;
static Node *gotos;

static char *brk_label;
static char *cont_label;

static Node *current_switch;

typedef struct VarScope VarScope;

struct VarScope {
    VarScope *next;
    char *name;
    Obj *var;
    Type *type_def;
    Type *enum_ty;
    int enum_val;
};

typedef struct {
    bool is_typedef;
    bool is_static;
    bool is_extern;
    int align;
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

typedef struct Initializer Initializer;
struct Initializer{
    Initializer *next; // global variableに使う
    Type *ty;
    bool is_flexible;
    
    Node *expr;

    Initializer **children;
};

typedef struct InitDesg InitDesg;
struct InitDesg{
    InitDesg *next;
    int idx;
    Member *member; // struct
    Obj  *var;
};

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

/* トークンの名前をバッファに格納してポインタを返す。strndupと同じ動作。 */
static char* get_ident(Token* tok){
    if(tok -> kind != TK_IDENT)
        error_at(tok -> str, "expected an identifier\n");
    char* name = calloc(1, tok -> len + 1); // null終端するため。
    return strncpy(name, tok -> str, tok -> len);
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
static Type *find_typedef(Token *tok){
    if(tok -> kind == TK_IDENT){
        VarScope *sc = find_var(tok);
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
    var -> align = ty -> align;
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
    gvar -> is_definition = true;
    gvar -> is_global = true;
    gvar -> is_static = true;
    gvar -> next = globals;
    globals = gvar;
    return gvar;
}

static char* new_unique_name(void){
    static int idx;
    char *buf = calloc(1, 10);
    sprintf(buf, ".L.%d", idx);
    idx++;
    return buf;
}

static Obj *new_anon_gvar(Type *ty){
    return new_gvar(new_unique_name(), ty);
}

static Obj *new_string_literal(Token *tok){
    Obj *strl = new_anon_gvar(tok -> ty);
    strl -> init_data = tok -> str;
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
static Node* new_unary(NodeKind kind, Node *lhs){
    Node* node = new_node(kind);
    node -> lhs = lhs;
    return node;
}

/* ND_NUMを作成 */
static Node* new_num_node(int64_t val){
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

static bool is_typename(Token *tok);
static Type* declspec(VarAttr *attr);
static Type *typename(void);
static Type* func_params(Type *ret_ty);
static Type* type_suffix(Type *ty);
static Type *array_dementions(Type *ty);
static Type* declarator(Type *ty);
static Type *abstract_declarator(Type *ty);
static Node *expr_stmt(void);
static Node* stmt(void);
static Node* compound_stmt(void);
static Node* declaration(Type *base, VarAttr *attr);
static void assign_initializer(Initializer *init);
static Node *lvar_initializer(Obj *var);
static void gvar_initialzier(Obj *var);
static int64_t eval(Node *node, char **label);
static int64_t const_expr(void);
static Node* expr(void);
static Node* assign(void);
static Node *conditional(void);
static Node *logor(void);
static Node *logand(void);
static Node *bitor(void);
static Node *bitxor(void);
static Node *bitand(void);
static Node* equality(void);
static Node * relational(void);
static Node *shift(void);
static Node* new_add(Node *lhs, Node *rhs);
static Node* new_sub(Node *lhs, Node *rhs);
static Node* add(void);
static Node* mul(void);
static Node *cast(void);
static Node* unary(void);
static Node* postfix(void);
static Node* primary(void);
static Node* funcall(void);

/*  typedefは変数定義と同じくtypedef int x, *y;のように書ける。
    chibiccにはtypedef int;のようなテストケースがあるが、役に立たないのでこういう入力は受け付けないことにする。*/
static void parse_typedef(Type *base){
    do{
        Type *ty = declarator(base);
        push_scope(get_ident(ty -> name)) -> type_def = ty;
    }while(consume(","));
    expect(";");
}

// tokenを先読みして関数かどうか調べる
static bool is_function(void){
    if(is_equal(token, ";"))
        return false;
    Token *tok = token;
    Type dummy = {};
    Type *ty = declarator(&dummy);
    token = tok;
    return ty -> kind == TY_FUNC;
}

/* ty->paramsは arg1->arg2->arg3 ...のようになっている。これを素直に前からnew_lvarを読んでいくと、localsは arg3->arg2->arg1という風になる。関数の先頭では渡されたパラメータを退避する必要があり、そのためにはlocalsをarg1->arg2->arg3のようにしたい。そこでty->paramsの最後の要素から生成している。*/
static void create_param_lvars(Type* param){
    if(param){
        create_param_lvars(param -> next);
        new_lvar(get_ident(param -> name), param);
    }
}

static void resolve_goto_labels(void){
    for(Node *x = gotos; x; x = x -> goto_next){
        for(Node *y = labels; y; y = y -> goto_next){
            if(!strcmp(x -> label, y -> label)){
                x -> unique_label = y -> unique_label;
                break;
            }
        }
        if(!x -> unique_label)
            error("use of undeclaraed label");
    }
    gotos = labels = NULL;
}

// function = declarator ( ";" | "{" compound_stmt)
static void function(Type *base, VarAttr *attr){
    Type *ty = declarator(base);

    Obj* func = new_gvar(get_ident(ty -> name), ty);
    func -> is_definition = !consume(";");
    func -> is_static = attr -> is_static;

    if(!func -> is_definition)
        return;
    
    current_fn = func;
    locals = NULL;
    enter_scope(); //仮引数を関数のスコープに入れるため。
    create_param_lvars(ty -> params);
    func -> params = locals; // 可変長引数はparamには含まない。

    if(ty -> is_variadic)
        func -> va_area = new_lvar("__va_area__", array_of(ty_char, 136));
    
    expect("{");
    func -> body = compound_stmt();
    func-> locals = locals;
    leave_scope();
    resolve_goto_labels();
}

// global_variable = declarator ( "=" global-initialzier )? ("," declarator ("=" global-initialzier )? )* 
static void global_variable(Type *base, VarAttr *attr){
    bool is_first = true;
    while(!consume(";")){
        if(!is_first)
            expect(",");
        is_first = false;
        Type *ty = declarator(base);
        Obj *var = new_gvar(get_ident(ty -> name), ty);
        var -> is_definition = !attr -> is_extern;
        var -> is_static = attr -> is_static;
        
        if(attr -> align)
            var -> align = attr -> align;
    
        if(consume("="))
            gvar_initialzier(var);
    }
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
        
        if(is_function()){
            function(base, &attr);
            continue;
        }
        
        global_variable(base, &attr);
    }
    return globals;
}

/* expr-stmt = expr? ";" */
static Node *expr_stmt(void){
    if(consume(";")){
        return new_node(ND_BLOCK); // null statement
    }
    Node *node = new_node(ND_EXPR_STMT);
    node -> lhs = expr();
    expect(";");
    return node;
}

/* stmt = "return" expr? ";" 
        | "if" "(" expr ")" stmt ("else" stmt)?
        | "while" "(" expr ")" stmt
        | "do" stmt "while" "(" expr ")" ";"
        | "for" "(" expr? ";" expr? ";" expr? ")" stmt 
        | "goto" ident 
        | ident ":" stmt
        | "break" ";"
        | "continue" ";"
        | switch "(" expr ")" stmt
        | "case" const-expr ":" stmt
        | "default" ":" stmt
        | "{" compound-stmt
        | expr-stmt */
static Node* stmt(void){

    if(consume("return")){
        Node *node = new_node(ND_RET);
        if(consume(";"))
            return node;
        
        Node *exp = expr();
        add_type(exp);
        expect(";");
        node -> lhs = new_cast(exp, current_fn -> ty -> ret_ty);
        return node;
    }

    if(consume("if")){
        expect("(");
        Node *node = new_node(ND_IF);
        node -> cond = expr();
        expect(")");
        node -> then = stmt();
        if(consume("else")){
            node -> els = stmt();
        }
        return node;
    }

    if(consume("while")){
        Node *node = new_node(ND_FOR);
        char *brk = brk_label;
        char *cont = cont_label;
        brk_label = node -> brk_label = new_unique_name();
        cont_label = node -> cont_label = new_unique_name();
        expect("(");
        node -> cond = expr();
        expect(")");
        node -> then = stmt();
        brk_label = brk;
        cont_label = cont;
        return node;
    }

    if(consume("do")){
        Node *node = new_node(ND_DO);
        char *brk = brk_label;
        char *cont = cont_label;
        brk_label = node -> brk_label = new_unique_name();
        cont_label = node -> cont_label = new_unique_name();
        node -> then = stmt();

        brk_label = brk;
        cont_label = cont;

        expect("while");
        expect("(");
        node -> cond = expr();
        expect(")");
        expect(";");
        return node;
    }

    if(consume("for")){
        enter_scope();
        Node *node = new_node(ND_FOR);
        char *brk = brk_label;
        char *cont = cont_label;
        brk_label = node -> brk_label = new_unique_name();
        cont_label = node -> cont_label = new_unique_name();
        expect("(");
        if(is_typename(token)){
            Type *base = declspec(NULL);
            node -> init = declaration(base, NULL);
        }else{
            node -> init = expr_stmt();
        }
        if(!is_equal(token, ";")){
            node -> cond = expr();
        }
        expect(";");
        if(!is_equal(token, ")")){
            node -> inc = expr();
        }
        expect(")");
        node -> then = stmt();
        brk_label = brk;
        cont_label = cont;
        leave_scope();
        return node;
    }

    if(consume("goto")){
        Node *node = new_node(ND_GOTO);
        node -> label = get_ident(token);
        next_token();
        expect(";");
        node -> goto_next = gotos;
        gotos = node;
        return node;
    }

    if(token -> kind == TK_IDENT && is_equal(token -> next, ":")){
        Node *node = new_node(ND_LABEL);
        node -> label = get_ident(token);
        node -> unique_label = new_unique_name();
        next_token();
        expect(":");
        node -> lhs = stmt();
        node -> goto_next = labels;
        labels = node;
        return node;
    }

    if(consume("break")){
        if(!brk_label)
            error("stary break");
        Node *node = new_node(ND_GOTO);
        node -> unique_label = brk_label;
        expect(";");
        return node;
    }

    if(consume("continue")){
        if(!cont_label)
            error("stary continue");
        Node *node = new_node(ND_GOTO);
        node -> unique_label = cont_label;
        expect(";");
        return node;
    }

    if(consume("switch")){
        Node *sw = current_switch;
        Node *node = current_switch = new_node(ND_SWITCH);
        char *brk = brk_label;
        brk_label = node -> brk_label = new_unique_name(); 
        expect("(");
        node -> cond = expr();
        expect(")");
        node -> then = stmt();
        brk_label = brk;
        current_switch = sw;
        return node;
    }

    if(consume("case")){
        if(!current_switch)
            error("stray case");
        
        Node *node = new_node(ND_CASE);
        node -> val = const_expr();
        expect(":");

        node -> unique_label = new_unique_name();
        node -> lhs = stmt();

        /* リストに登録 */
        node -> case_next = current_switch -> case_next;
        current_switch -> case_next = node;

        return node;
    }

    if(consume("default")){
        if(!current_switch)
            error("stary default");
        if(current_switch -> default_case)
            error("muliple default labels in one switch");
        
        expect(":");
        Node *node = new_node(ND_CASE);
        node -> unique_label = new_unique_name();
        node -> lhs = stmt();
        current_switch -> default_case = node;
        return node;
    }

    if(consume("{")){
        return compound_stmt();
    }

    return expr_stmt();
}

static bool is_typename(Token *tok){
    static char* kw[] = {"void", "char", "short", "int", "long", "void", "struct", "union", "typedef", "_Bool", "enum", "static", "extern", "_Alignas"};
    for(int i =0; i < sizeof(kw) / sizeof(*kw); i++){
        if(is_equal(tok, kw[i])){
            return true;
        }
    }
    return find_typedef(tok);
}

/* compound-stmt = (declaration | stmt)* "}" */
static Node* compound_stmt(void){
    Node *node = new_node(ND_BLOCK);
    Node head = {};
    Node *cur = &head;
    enter_scope();
    while(!consume("}")){
        if(is_typename(token) && !is_equal(token -> next, ":")){
            VarAttr attr = {};
            Type *base = declspec(&attr);
            if(attr.is_typedef){
                parse_typedef(base);
                continue;
            }

            if(attr.is_extern){
                global_variable(base, &attr);
                continue;
            }

            if(is_function()){
                function(base, &attr);
                continue;
            }

            cur = cur -> next = declaration(base, &attr);
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

// struct-members = (declspec declarator ( "," declarator)* ";" )* "}"
static void struct_members(Type *ty){
    Member head = {};
    Member *cur = &head;
    int idx = 0;
    while(!consume("}")){
        VarAttr attr = {};
        Type *base = declspec(&attr);
        bool is_first = true;
        while(!consume(";")){
             if(!is_first)
                expect(",");
            is_first = false;

            struct Member *mem = calloc(1, sizeof(Member));
            mem -> ty = declarator(base);
            mem -> name = mem -> ty -> name;
            mem -> idx = idx++;
            mem -> align = attr.align? attr.align : mem -> ty -> align;
            cur = cur -> next = mem;
        }
    }

    // flexible array member 
    // cur != &headのチェックはなぜ必要?
    // struct {int x[];} x;のような入力も通ってしまう。
    if(cur != &head && cur -> ty -> kind == TY_ARRAY && cur -> ty -> array_len < 0){
        cur -> ty = array_of(cur -> ty -> base, 0);
        ty -> is_flexible = true; // fleble array memeberを持っていることを記録
    }

    ty -> members = head.next;
} 

/* struct-decl = ident? ("{" struct-members)? */
static Type *struct_union_decl(void){
    Token *tag = NULL;

    if(is_ident()){
        tag = token;
        next_token();
    }

    if(tag && !is_equal(token, "{")){
        Type *ty = find_tag(tag);
        if(ty)
            return ty;
        ty = struct_type();
        ty -> size = -1;
        push_tag_scope(get_ident(tag), ty);
        return ty;
    }

    expect("{");
    Type *ty = struct_type();
    struct_members(ty);

    if(tag){
        /* 現在のスコープに同名のタグがある場合。不完全型なので上書き */
        for(TagScope *tsc = scope -> tags; tsc; tsc = tsc -> next){
            if(is_equal(tag, tsc -> name)){
                *tsc -> ty = *ty; // 不完全型を修正
                return tsc -> ty;
            }
        }
        push_tag_scope(get_ident(tag), ty);
    }

    return ty;
}

/* struct-decl = struct-union-decl */
static Type *struct_decl(void){
    Type *ty = struct_union_decl();
    ty -> kind = TY_STRUCT;

    /* 不完全型なら何もしない */
    if(ty -> size < 0)
        return ty;
    
    int offset = 0;
    for(Member *m = ty -> members; m; m = m -> next){
        offset = align_to(offset, m -> align);
        m -> offset = offset;
        offset += m -> ty -> size;
        /* 構造体のalignmentは最もaligmenが大きいメンバに合わせる。*/
        if(ty -> align < m -> align){
            ty -> align = m -> align;
        }
    }
    ty -> size = align_to(offset, ty -> align);
    return ty;
}

/* union-decl = struct-union-decl */
static Type *union_decl(void){
    Type *ty = struct_union_decl();
    ty -> kind = TY_UNION;

     /* 不完全型なら何もしない */
    if(ty -> size < 0)
        return ty;
    
    for(Member *m = ty -> members; m; m = m -> next){
        if(ty -> size < m -> ty -> size){
            ty -> size = m -> ty -> size; // 共用体のサイズは最も大きいメンバに合わせる。
        }
        if(ty -> align < m -> align){
            ty -> align = m -> align;
        }
    }
    return ty;
}

static bool consume_end(void){
    if(consume("}"))
        return true;
    if(is_equal(token, ",") && is_equal(token -> next, "}")){
        token = token -> next -> next;
        return true;
    }
}

static bool is_end(void){
    return is_equal(token, "}") || (is_equal(token, ",") && is_equal(token -> next, "}"));
}

/*  enum-specifier   = ident? "{" enum-list? "}"
                    | ident
    enum-list       = enumerator ("," enumerator)* ","?
    enumerator      = ident ( "=" const-expression )? */
static Type *enum_specifier(void){
    Token *tag = NULL;
    Type *ty = enum_type();

    if(token -> kind == TK_IDENT){
        tag = token;
        next_token();
    }

    if(tag && !is_equal(token, "{")){
        ty = find_tag(tag);
        if(!ty)
            error_at(token -> str, "unknown enum type\n");
        if(ty -> kind != TY_ENUM)
            error_at(token -> str, "not an enum type tag\n");
        return ty;
    }

    expect("{");

    int64_t val = 0;
    while(!consume_end()){
        char *name = get_ident(token);
        next_token();
        if(consume("=")){
            val = const_expr();
        }
        VarScope *vsc = push_scope(name);
        vsc -> enum_ty = ty;
        vsc -> enum_val = val++;
        consume(",");
    }

    if(tag)
        push_tag_scope(get_ident(tag), ty);
    return ty;
}

/*  declspec    = ("void" | "char" | "short" | "int" | "long" | "_Bool"
                | struct-decl 
                | union-decl 
                | typedef-name
                | enum-specifier )+ */
static Type* declspec(VarAttr *attr){
    enum{
        BOOL = 1 << 0,
        VOID = 1 << 2,
        CHAR = 1 << 4,
        SHORT = 1 << 6,
        INT = 1 << 8,
        LONG = 1 << 10
    };

    int counter = 0;
    Type *ty = ty_int; // typedef tのように既存の型が指定されていない場合、intになる。

    /* counterの値を調べているのはint main(){ typedef int t; {typedef long t;} }のように同名の型が来た時に二回目のtでfind_typedef()がtrueになってしまうから。*/
    while(is_typename(token)){

        /* handle strorage class specifiers */
        if(is_equal(token, "typedef") || is_equal(token, "static") || is_equal(token, "extern")){
            if(!attr){
                error_at(token -> str, "storage class specifier is not allowed in this context");
            }
            if(is_equal(token, "typedef"))
                attr -> is_typedef = true;
            else if(is_equal(token, "static"))
                attr -> is_static = true;
            else
                attr -> is_extern = true;

            if(attr -> is_typedef && attr -> is_static + attr -> is_extern > 1)
                error_at(token -> str, "typedef may not be used with static or extern\n");
            token = token -> next;
            continue;
        }

        // "Alignas" "(" num | typename ")" 
        if(consume("_Alignas")){
            expect("(");
            if(is_typename(token))
                attr -> align = typename() -> align;
            else 
                attr -> align = const_expr();
            expect(")");
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
        if(consume("enum")){
            return enum_specifier();
        }

        if(consume("_Bool")){
            counter += BOOL;
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
            case BOOL:
                ty = ty_bool;
                break;

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

/* func-params = ( "void" | param ("," param)* ("," "...")?)? ")"
 param       = type-specifier declarator*/
static Type* func_params(Type *ret_ty){ 
    // func(void)は引数を取らないことを意味する。
    if(is_equal(token, "void") && is_equal(token -> next, ")")){
        token = token -> next ->next;
        return func_type(ret_ty);
    }

    Type head = {};
    Type *cur = &head;
    bool is_variadic = false;

    while(!consume(")")){
        if(cur != &head)
            expect(",");
        
        if(consume("...")){
            is_variadic = true;
            expect(")");
            break;
        }

        Type *ty = declspec(NULL); // 仮引数にtypedefは来れない。
        ty = declarator(ty);

        /* array of T を pointer to T に変換する */
        if(ty -> kind == TY_ARRAY){
            Token *name = ty -> name;
            ty = pointer_to(ty -> base);
            ty -> name = name;
        }

        cur = cur -> next = copy_type(ty); // copyしないと上書きされる可能性があるから。
    }

    Type *func = func_type(ret_ty);
    func -> params = head.next;
    func -> is_variadic = is_variadic;
    return func;
}

/* type-suffix  = "(" func-params 
                | "[" array-dementions
                | ε */ 
static Type* type_suffix(Type *ty){
    if(consume("(")){
        return func_params(ty);
    }
    if(consume("["))
        return array_dementions(ty);
    return ty;
}

/* array-dementions = const-expr? "}" type-suffix */
static Type *array_dementions(Type *ty){
    if(consume("]")){
        ty = type_suffix(ty);
        return array_of(ty, -1);
    }
    int siz = const_expr();
    expect("]");
    ty = type_suffix(ty);
    return array_of(ty , siz);
}

/* char (*a) [2];を考える。*aを読んだ段階ではこれが何のポインタなのか分からない。()がある場合は外側を先に確定させる必要がある。この例だと一旦()を無視して、int [2]を読んでint型の配列(要素数2)が確定する。次に()の中を読むことでaの型がintの配列(要素数2)へのポインタ型だと分かる。*/

/* declarator = "*"*  (ident | "(" ident ")" | "(" declarator ")" ) type-suffix */
static Type* declarator(Type *ty){
    while(consume("*"))
        ty = pointer_to(ty);

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

    if(token -> kind != TK_IDENT){
        error_at(token -> str, "expected a variable name\n");
    }

    Token *name = token;
    next_token();
    ty = type_suffix(ty);
    ty -> name = name;
    return ty;
}

/* abstract-declarator = "*"* ("(" abstract-declarator ")")? type-suffix */
static Type *abstract_declarator(Type *ty){
    while(consume("*"))
        ty = pointer_to(ty);
    
    if(consume("(")){
        Token *start = token;
        Type dummy = {};
        abstract_declarator(&dummy); // とりあえず読み飛ばす
        expect(")");
        ty = type_suffix(ty); // ()の外側の型を確定させる。
        Token *end = token;
        token  = start;
        ty = abstract_declarator(ty); // ()の中の型を確定させる。
        token = end;
        return ty;
    }

    return type_suffix(ty);
}

static Type *typename(void){
    Type *base = declspec(NULL);
    return abstract_declarator(base);
}

/* declspec declarator ("=" initalizer)? ("," declarator ("=" intializer)?)* ";" */
static Node *declaration(Type *base, VarAttr *attr){
    Node head = {};
    Node *cur = &head;
    while(!consume(";")){
        Type* ty = declarator(base);

        if(is_void(ty))
            error_at(ty -> name -> str, "variable declared void");

        if(attr && attr -> is_static){
            Obj *var = new_anon_gvar(ty);
            push_scope(get_ident(ty -> name)) -> var = var;
            if(consume("="))
                gvar_initialzier(var);
            continue;
        }

        Obj *lvar = new_lvar(get_ident(ty -> name), ty);

        if(attr && attr -> align)
            lvar -> align = attr -> align;
        
        if(consume("=")){
            Node *expr = lvar_initializer(lvar);
            cur = cur -> next  = new_unary(ND_EXPR_STMT, expr);
        }

        if(lvar -> ty -> size < 0)
             error_at(ty -> name -> str, "variable has incomplete type");

        if(consume(",")){
            continue;
        }
    }
    Node *node = new_node(ND_BLOCK);
    node -> body = head.next;
    return node;
}

static Initializer *new_initializer(Type *ty, bool is_flexible){
    Initializer *init = calloc(1, sizeof(Initializer));
    init -> ty = ty;
    if(ty -> kind == TY_ARRAY){
        // 要素数の省略が許されるかつ要素数が指定されていない場合
        if(is_flexible && ty -> size < 0){
            init -> is_flexible = true;
            return init;
        }
        init -> children = calloc(ty -> array_len, sizeof(Initializer));
        for(int i = 0; i < ty -> array_len; i++){
            init -> children[i] = new_initializer(ty -> base, false);
        }
    }
    if(ty -> kind == TY_STRUCT || ty -> kind == TY_UNION){
        int len = 0;
        for(Member *mem = ty -> members; mem; mem = mem -> next)
            len++;
        init -> children = calloc(len, sizeof(Initializer*));
        for(Member *mem = ty -> members; mem; mem = mem -> next){
            if(is_flexible && ty -> is_flexible){
                Initializer *child = calloc(1, sizeof(Initializer));
                child -> ty = mem -> ty;
                child -> is_flexible = true;
                init -> children[mem -> idx] = child;
            }else{
                init -> children[mem -> idx] = new_initializer(mem -> ty, false);
            }
        }
    }
    return init;
}

// {が出てきたら}まで読み飛ばす。それ以外はassignか文字列リテラルを一つ読み飛ばす。
static void skip_excess_element(void){
    if(consume("{")){
        for(int i = 0; !consume("}"); i++){
            if(0 < i)
                expect(",");
            skip_excess_element();
        }
        return;
    }
    if(token -> kind == TK_STR)
        next_token();
    else 
        assign();
}

// stirng-intizlier = string-literal
static void string_initializer(Initializer *init){
    // 要素数が指定されていない場合修正
    if(init -> is_flexible)
        *init = *new_initializer(array_of(ty_char, token -> ty -> array_len), false);
    
    int len = MIN(init -> ty -> array_len, token -> ty -> array_len);
    
    for(int i = 0; i < len; i++){
        init -> children[i] -> expr = new_num_node(token -> str[i]);
    }
    next_token();
}

static int count_array_init_elements(Type *ty){
    Token * tok = token; // tokenを保存しておく。(assing_initializerはtokenを変更してしますため)
    Initializer *dummy = new_initializer(ty -> base, false);
    int i = 0;

    for(;!consume_end(); i++){
        if(0 < i)
            expect(",");
        assign_initializer(dummy);
    }
    token = tok;
    return i;
}

// array-initializer1 = "{" initializer ("," initizlier )* ","? }"
static void array_initializer1(Initializer *init){
    expect("{");

    if(init -> is_flexible){
        int len = count_array_init_elements(init -> ty);
        *init = *new_initializer(array_of(init -> ty -> base, len), false);
    }

    for(int i = 0; !consume_end(); i++){
        if(0 < i)
            expect(",");

        if(i < init -> ty -> array_len)
            assign_initializer(init -> children[i]);
        else 
            skip_excess_element();
    }
}

// array-initializer2 = initializer ("," initizlier )*
static void array_initializer2(Initializer *init){

    if(init -> is_flexible){
        int len = count_array_init_elements(init -> ty);
        *init = *new_initializer(array_of(init -> ty -> base, len), false);
    }

    for(int i = 0; i < init -> ty -> array_len && !is_end(); i++){
        if(0 < i)
            expect(",");
        assign_initializer(init -> children[i]);
    }
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void struct_initializer1(Initializer *init){
    expect("{");
    Member *mem = init -> ty -> members;
    while(!consume_end()){
        if(mem != init -> ty -> members)
            expect(",");
        
        if(mem){
            assign_initializer(init -> children[mem -> idx]);
            mem = mem -> next;
        }else{
            skip_excess_element();
        }
    }
}

// struct-initializer2 = initializer ("," initializer)* 
static void struct_initializer2(Initializer *init){
    bool is_first = true;
    for(Member *mem = init -> ty -> members; mem && !is_end(); mem = mem -> next){
        if(!is_first)
            expect(",");
        is_first = false;
        assign_initializer(init -> children[mem -> idx]);
    }
}

// union-initializer = "{" initializer ","? "}" | initializer"
static void union_initializer(Initializer *init){
    if(consume("{")){
        assign_initializer(init -> children[0]);
        consume(",");
        expect("}");
        return;
    }
    assign_initializer(init -> children[0]);
}

// initializer  = stirng-initializer
//              | array-initialzier
//              | struct-initializer | union-initializer
//              | assign
static void assign_initializer(Initializer *init){
    if(init -> ty -> kind == TY_ARRAY && token -> kind == TK_STR){
        string_initializer(init);
        return;
    }

    if(init -> ty -> kind == TY_ARRAY){
        if(is_equal(token, "{"))
            array_initializer1(init);
        else 
            array_initializer2(init);
        return;
    }

    if(init -> ty -> kind == TY_STRUCT){
        if(is_equal(token, "{")){
            struct_initializer1(init);
            return;
        }

        if(!is_equal(token, "{")){
            Token *tok = token;
            Node *expr = assign();
            add_type(expr);
            if(expr -> ty -> kind == TY_STRUCT){
                init -> expr = expr;
                return;
            }
            token = tok;
        }

        struct_initializer2(init); // {が省略された構造体の初期化式
        return;
    }

    if(init -> ty -> kind == TY_UNION){
        union_initializer(init);
        return;
    }

    if(consume("{")){
        assign_initializer(init); // init -> expr = assign()じゃだめなのか?
        expect("}");
        return;
    }

    init -> expr = assign();
}

static Node *create_target(InitDesg *desg){
    if(desg -> var)
        return new_var_node(desg -> var);
    if(desg -> member){
        Node * node = new_unary(ND_MEMBER, create_target(desg -> next));
        node -> member = desg -> member;
        return node;
    }
    Node *lhs = create_target(desg -> next);
    Node *rhs = new_num_node(desg -> idx);
    return new_unary(ND_DEREF, new_add(lhs, rhs));
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg){
    if(ty -> kind == TY_ARRAY){
        Node *node = new_node(ND_NULL_EXPR);
        for(int i = 0; i < ty -> array_len; i++){
            InitDesg desg2 = {desg, i};
            Node *rhs = create_lvar_init(init -> children[i], ty -> base, &desg2);
            node = new_binary(ND_COMMA, node, rhs);
        }
        return node;
    }

    if(ty -> kind == TY_STRUCT && !init -> expr){
        Node *node = new_node(ND_NULL_EXPR);
        for(Member *mem = ty -> members; mem; mem = mem -> next){
            InitDesg desg2  = {desg, 0, mem};
            Node *rhs = create_lvar_init(init -> children[mem -> idx], mem -> ty, &desg2);
            node = new_binary(ND_COMMA, node, rhs);
        }
        return node;
    }

    if(ty -> kind == TY_UNION){
        InitDesg desg2 = {desg, 0, ty -> members};
        return create_lvar_init(init -> children[0], ty -> members -> ty, &desg2);
    }

    // 初期化式が明示されていなければ代入式を作る必要はない
    if(!init -> expr)
        return new_node(ND_NULL_EXPR);
    
    // 初期化式が指定されていれば代入式を作る
    Node *lhs = create_target(desg);
    return new_binary(ND_ASSIGN, lhs, init -> expr);
}

static Type *copy_struct_type(Type *ty){
    ty = copy_type(ty); // copyは必須。元の型情報を残して置く必要があるため。
    Member head = {};
    Member *cur = &head;
    for(Member *mem = ty -> members; mem; mem = mem -> next){
        Member *m = calloc(1, sizeof(Member));
        *m = *mem;
        cur = cur -> next = m;
    }
    ty -> members = head.next;
    return ty;
}

// ローカル変数、グローバル変数共用
static Initializer *initializer(Obj *var){
    Initializer *init = new_initializer(var -> ty, true);
    assign_initializer(init);

    // flexible array memberを持っている場合、var -> tyを修正する。
    if((var -> ty -> kind == TY_STRUCT || var -> ty -> kind == TY_UNION) && var -> ty -> is_flexible){
        var -> ty = copy_struct_type(var -> ty);
        Member *mem = var -> ty -> members;
        while(mem -> next)
            mem = mem -> next; // flexible array memberまで移動
        mem -> ty = init -> children[mem -> idx] -> ty;
        var -> ty -> size += mem -> ty -> size;
        return init;
    }

    var -> ty = init -> ty;
    return init;
}

static Node *lvar_initializer(Obj *var){
    Initializer *init = initializer(var);

    InitDesg desg = {NULL, 0, NULL, var};
    
    // 先頭で配列を0クリアする
    Node *lhs = new_node(ND_MEMZERO);
    lhs -> var = var;
    
    Node *rhs = create_lvar_init(init, var -> ty, &desg);
    return new_binary(ND_COMMA, lhs, rhs);
}

static void write_buf(char *buf, int64_t val, int size){
    if(size == 1){
        *buf = val;
    }else if(size == 2){
        *(int16_t*)buf = val;
    }else if(size == 4){
        *(int32_t*)buf = val;
    }else if(size == 8){
        *(int64_t*)buf = val;
    }else{
        assert(0); // unreachable
    }
}

static Relocation *write_gvar_data(Relocation *cur, Initializer *init, Type *ty, char *buf, int offset){
    if(ty -> kind == TY_ARRAY){
        int size = ty -> base -> size;
        for(int i = 0; i < ty -> array_len; i++)
            cur = write_gvar_data(cur, init -> children[i], ty -> base, buf, offset + size * i);
        return cur;
    }

    if(ty -> kind == TY_STRUCT){
        for(Member *mem = ty -> members; mem; mem = mem -> next)
            cur = write_gvar_data(cur, init -> children[mem -> idx], mem -> ty, buf, offset + mem -> offset);
        return cur;
    }

    if(ty -> kind == TY_UNION){
        return write_gvar_data(cur, init -> children[0], ty -> members -> ty, buf, offset);
    }
    
    if(!init -> expr)
        return cur;
    
    char *label = NULL;
    int64_t val = eval(init -> expr, &label);

    if(!label){
        write_buf(buf + offset, val, ty -> size);
        return cur;
    }

    Relocation *rel = calloc(1, sizeof(Relocation));
    rel -> offset = offset;
    rel -> label = label;
    rel -> addend = val;
    cur -> next = rel;
    return cur -> next;
}

static void gvar_initialzier(Obj *var){
    Relocation head = {};
    Initializer *init = initializer(var);
    char *buf = calloc(1, var -> ty -> size);
    write_gvar_data(&head, init, var -> ty, buf, 0);
    var -> init_data = buf;
    var -> rel = head.next;
}

static int64_t eval_rval(Node *node, char** label){
    switch(node -> kind){
        case ND_VAR:
            if(!node -> var -> is_global)
                error("not a compile-time constant");
            *label = node -> var -> name;
            return 0;
        case ND_DEREF:
            return eval(node -> lhs, label);
        case ND_MEMBER:
            return eval_rval(node -> lhs, label) + node -> member -> offset;
    }
    error("invalid initializer");
}

// 構文木を下りながら計算して値を返す 
static int64_t eval(Node *node, char** label){
    add_type(node);

    switch(node -> kind){
        case ND_ADD:
            return eval(node -> lhs, label) + eval(node -> rhs, NULL);
        case ND_SUB:
            return eval(node -> lhs, label) - eval(node -> rhs, NULL);
        case ND_MUL:
            return eval(node -> lhs, NULL) * eval(node -> rhs, NULL);
        case ND_DIV:
            return eval(node -> lhs, NULL) / eval(node -> rhs, NULL);
        case ND_MOD:
            return eval(node -> lhs, NULL) % eval(node -> rhs, NULL);
        case ND_EQ:
            return eval(node -> lhs, NULL) == eval(node -> rhs, NULL);
        case ND_NE:
            return eval(node -> lhs, NULL) != eval(node -> rhs, NULL);
        case ND_LT:
            return eval(node -> lhs, NULL) < eval(node -> rhs, NULL);
        case ND_LE:
            return eval(node -> lhs, NULL) <= eval(node -> rhs, NULL);
        case ND_NEG:
            return -eval(node -> lhs, NULL);
        case ND_COND:
            return eval(node -> cond, NULL) ? eval(node -> then, label) : eval(node -> els, label);
        case ND_NOT:
            return !eval(node -> lhs, NULL);
        case ND_BITNOT:
            return ~eval(node -> lhs, NULL);
        case ND_BITOR:
            return eval(node -> lhs, NULL) | eval(node -> rhs, NULL);
        case ND_BITXOR:
            return eval(node -> lhs, NULL) ^ eval(node -> rhs, NULL);
        case ND_BITAND: 
            return eval(node -> lhs, NULL) & eval(node -> rhs, NULL);
        case ND_SHL:
            return eval(node -> lhs, NULL) << eval(node -> rhs, NULL);
        case ND_SHR:
            return eval(node -> lhs, NULL) >> eval(node -> rhs, NULL);
        case ND_LOGAND:
            return eval(node -> lhs, NULL) && eval(node -> rhs, NULL);
        case ND_LOGOR:
            return eval(node -> lhs, NULL) || eval(node -> rhs, NULL);
        case ND_COMMA:
            return eval(node -> rhs, label);
        case ND_CAST:{
            int64_t val = eval(node -> lhs, label);
            if(is_integer(node -> ty)){
                switch(node -> ty -> size){
                    case 1:
                        return (int8_t)val;
                    case 2:
                        return (int16_t)val;
                    case 4:
                        return (int32_t)val;
                }
            }
            return val;
        }
        case ND_ADDR:
            return eval_rval(node -> lhs, label);
        case ND_MEMBER:
            if(!label)
                error("not a compile-time constant");
            if(node -> ty -> kind != TY_ARRAY)
                error("invalid initializer");
            return eval_rval(node -> lhs, label) + node -> member -> offset;
        case ND_VAR:
            if(!label)
                error("not a compile-time constant");
            if(node -> var -> ty -> kind != TY_ARRAY && node -> var -> ty -> kind != TY_FUNC)
                error("invalid initializer");
            *label = node -> var -> name;
            return 0;
        case ND_NUM:
            return node -> val;
    }  
    error("not a compile-time constant");
}

// const-expr = conditional
static int64_t const_expr(void){
    Node* node = conditional();
    return eval(node, NULL);
}

/* expr = assign ("," expr)? */
static Node* expr(void){
    Node *node = assign();
    if(consume(","))
        node = new_binary(ND_COMMA, node, expr());
    return node;
}

/* A op= Bを、tmp = &A, *tmp = *tmp op Bに変換する */
static Node *to_assign(Node *binary){
    add_type(binary -> lhs);
    add_type(binary -> rhs);
    Obj *var = new_lvar("", pointer_to(binary -> lhs -> ty));
    Node *expr1 = new_binary(ND_ASSIGN, 
                            new_var_node(var), 
                            new_unary(ND_ADDR, binary -> lhs));
    Node *expr2 = new_binary(ND_ASSIGN, 
                            new_unary(ND_DEREF, new_var_node(var)), 
                            new_binary(binary -> kind, new_unary(ND_DEREF, new_var_node(var)), binary -> rhs));
    return new_binary(ND_COMMA, expr1, expr2);
}

/*  assign = conditional (assing_op assign)?
    assing-op = "+=" | "-=" | "*=" | "/=" | "%=" | "|=" | "^=" | "&=" | "<<=" | ">>=" | "=" */
static Node* assign(void){
    Node* node = conditional();
    if(consume("+="))
        node = to_assign(new_add(node, assign()));
    if(consume("-="))
        node = to_assign(new_sub(node, assign()));
    if(consume("*="))
        node = to_assign(new_binary(ND_MUL, node, assign()));
    if(consume("/="))
        node = to_assign(new_binary(ND_DIV, node, assign()));
    if(consume("%="))
        node = to_assign(new_binary(ND_MOD, node, assign()));
    if(consume("|="))
        node = to_assign(new_binary(ND_BITOR, node, assign()));
    if(consume("^="))
        node = to_assign(new_binary(ND_BITXOR, node, assign()));
    if(consume("&="))
        node = to_assign(new_binary(ND_BITAND, node, assign()));
    if(consume("<<="))
        node = to_assign(new_binary(ND_SHL, node, assign()));
    if(consume(">>="))
        node = to_assign(new_binary(ND_SHR, node, assign()));
    if(consume("="))
        node = new_binary(ND_ASSIGN, node, assign());
    return node;
}

/* conditional = logor ("?" expr ":" conditional)? */
static Node *conditional(void){
    Node *cond = logor();
    if(!consume("?"))
        return cond;
    Node *node = new_node(ND_COND);
    node -> cond = cond;
    node -> then = expr();
    expect(":");
    node -> els = conditional();
    return node;
}

/* logor = logand ("||" logand)* */
static Node *logor(void){
    Node *node = logand();
    while(consume("||"))
        node = new_binary(ND_LOGOR, node, logand());
    return node;
}

/* logand = bitor ("&&" bior)* */
static Node *logand(void){
    Node *node = bitor();
    while(consume("&&"))
        node = new_binary(ND_LOGAND, node, bitor());
    return node;
}

/* bitor = bitxor ("|" bitxor )* */ 
static Node *bitor(void){
    Node *node = bitxor();
    while(consume("|"))
        node = new_binary(ND_BITOR, node, bitxor());
    return node;
}

/* bitxor = bitand ("^" binand)* */
static Node *bitxor(void){
    Node *node = bitand();
    while(consume("^"))
        node = new_binary(ND_BITXOR, node, bitand());
    return node;
}

/* bitand = equality ("&" equality)* */
static Node *bitand(void){
    Node *node = equality();
    while(consume("&"))
        node = new_binary(ND_BITAND, node, equality());
    return node;
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

/* relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)* */
static Node* relational(void){
    Node* node = shift();
    for(;;){
        if(consume("<")){
            node = new_binary(ND_LT, node, shift());
            continue;
        }
        if(consume("<=")){
            node = new_binary(ND_LE, node , shift());
            continue;
        }
        if(consume(">")){
            node = new_binary(ND_LT, shift(), node); /* x > y は y < xと同じ。 */
            continue;
        }
        if(consume(">=")){
            node = new_binary(ND_LE, shift(), node); /* x >= y は y <= xと同じ */
            continue;
        }
        return node;
    }
}

/* shift = add ("<<" shift || ">>" shift)* */
static Node *shift(void){
    Node *node = add();
    for(;;){
        if(consume("<<")){
            node = new_binary(ND_SHL, node, add());
            continue;
        }
        if(consume(">>")){
            node = new_binary(ND_SHR, node, add());
            continue;
        }
        return node;
    }
}

static Node *new_long(uint64_t val){
    Node *node = new_num_node(val);
    node -> ty = ty_long;
    return node;
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
        rhs = new_binary(ND_MUL, new_long(lhs -> ty -> base -> size), rhs);
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
        return new_binary(ND_DIV, node, new_long(lhs -> ty -> base -> size));
    }
    /* pointer - num は pointer - sizeof(type) * num */
    if(is_ptr(lhs -> ty) && is_integer(rhs -> ty)){
        rhs = new_binary(ND_MUL, new_long(lhs -> ty -> base -> size), rhs);
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

/* mul = cast ("*" cast | "/" cast | "%" cast)* */
static Node* mul(void){
    Node* node = cast();
    for(;;){
        if(consume("*")){
            node = new_binary(ND_MUL, node, cast());
            continue;
        }
        if(consume("/")){
            node = new_binary(ND_DIV, node, cast());
            continue;
        }
        if(consume("%")){
            node = new_binary(ND_MOD, node, cast());
            continue;
        }
        return node;
    }
}

Node *new_cast(Node *lhs, Type *ty){
    add_type(lhs); // from 
    Node *node = new_node(ND_CAST);
    node -> lhs = lhs;
    node -> ty = copy_type(ty); // to
    return node;
}

/* cast = ( typename ) cast | unary */
static Node *cast(void){
    if(is_equal(token, "(") && is_typename(token -> next)){
        Token *tok = token;
        consume("(");
        Type *ty = typename();
        expect(")");
        
        // compound literal
        if(is_equal(token , "{")){
            token = tok;
            return unary();
        }

        return new_cast(cast(), ty);
    }
    return unary();
}

/* ("+" | "-")? unaryになっているのは - - xのように連続する可能性があるから。*/
/* unary    = ("+" | "-" | "&" | "*" | "++" | "--" | "!" | "~")? cast
            | postfix */
static Node* unary(void){
    /* +はそのまま */
    if(consume("+")){
        return cast();
    }
    if(consume("-")){
        return new_unary(ND_NEG, cast());
    }
    if(consume("&")){
        return new_unary(ND_ADDR, cast());
    }
    if(consume("*")){
        return new_unary(ND_DEREF, cast());
    }
    if(consume("!"))
        return new_unary(ND_NOT, cast());
    if(consume("~"))
        return new_unary(ND_BITNOT, cast());
    if(consume("++"))
        return to_assign(new_add(cast(), new_num_node(1)));
    if(consume("--"))
        return to_assign(new_sub(cast(), new_num_node(1)));
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

/* A++を(typeof A)((A += 1) - 1)に変換する */
static Node *new_inc_dec(Node *node, int64_t added){
    add_type(node);
    return new_cast(new_add(to_assign(new_add(node, new_num_node(added))), new_num_node(-added)), node -> ty);
}

/* postfix  = "(" typename ")" "{" inititlizer-list "}"
            | primary ("[" expr "]" | "." ident | "->" ident | "++" | "--")* */
static Node* postfix(void){

    if(is_equal(token, "(") && is_typename(token -> next)){
        expect("(");
        Type *ty = typename();
        expect(")");

        if(scope -> next == NULL){
            Obj *var = new_anon_gvar(ty);
            gvar_initialzier(var);
            return new_var_node(var);
        }

        Obj *var = new_lvar("", ty);
        Node *lhs = lvar_initializer(var);
        Node *rhs = new_var_node(var);
        return new_binary(ND_COMMA, lhs, rhs);
    }

    Node *node = primary();
    for(;;){
        if(consume("[")){
            Node *idx = expr();
            node = new_unary(ND_DEREF, new_add(node, idx));
            expect("]");
            continue;
        }
        if(consume(".")){
            node = struct_ref(node, token);
            next_token();
            continue;
        }
        if(consume("->")){
            node = new_unary(ND_DEREF, node);
            node = struct_ref(node, token);
            next_token();
            continue;
        }
        if(consume("++")){
            node = new_inc_dec(node, 1);
            continue;
        }
        if(consume("--")){
            node = new_inc_dec(node, -1);
            continue;
        }
        return node;
    }
}

/* primary  = "(" expr ")" 
            | "(" "{" compound-stmt ")"
            | funcall
            | ident
            | str
            | "sizeof" typename
            | "sizeof" unary 
            | "Alignof" "(" type-name ")"
            | "Alignof" unary
            | num */
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

    if(consume("_Alignof")){
        if(is_equal(token, "(") && is_typename(token -> next)){
            expect("(");
            Type *ty = typename();
            expect(")");
            return new_num_node(ty -> align);
        }
        Node *node = unary();
        add_type(node);
        return new_num_node(node -> ty -> align);
    }

    if(is_ident()){
        if(is_equal(token -> next, "(")){
            return funcall();
        }
        VarScope *vsc = find_var(token);
        if(!vsc || (!vsc -> var && !vsc -> enum_ty)){
            error_at(token -> str, "undefined variable");
        }
        next_token();
        if(vsc -> var)
            return new_var_node(vsc -> var);
        else
            return new_num_node(vsc -> enum_val);
    }

    if(is_str()){
        Obj * str = new_string_literal(token);
        next_token();
        return new_var_node(str);
    }
    if(consume("sizeof")){
        if(is_equal(token, "(") && is_typename(token -> next)){
            next_token(); // '('を読み飛ばす
            Type *ty = typename();
            expect(")");
            return new_num_node(ty -> size);
        }else{
            Node *node = unary();
            add_type(node);
            return new_num_node(node -> ty -> size);
        }
    }

    if(token -> kind == TK_NUM){
        Node *node = new_num_node(token -> val);
        next_token();
        return node;
    }

    error_at(token -> str, "expected an expression\n");
}

/* funcall = ident "(" func-args? ")" */
static Node* funcall(void){
    VarScope *vsc = find_var(token);
    if(!vsc)
        error_at(token -> str, "implicit declaration of a function");
    if (!vsc -> var || vsc -> var -> ty -> kind != TY_FUNC)
        error_at(token -> str, "not a function");
    
    char *func_name = get_ident(token);
    Type *ty = vsc -> var -> ty;

    next_token();

    /* 引数のパース */
    Node head = {};
    Node *cur = &head;
    Type *param_ty = ty -> params;
    expect("(");
    /* 例えばf(1,2,3)の場合、リストは3->2->1のようにする。これはコード生成を簡単にするため。 */
    while(!consume(")")){
        Node *arg = assign();
        add_type(arg);
        /* int printf()のように、voidが子手入れされいない場合、任意の型を渡せる。*/
        if(param_ty){
            if(param_ty -> kind == TY_STRUCT || param_ty -> kind == TY_UNION){
                error("passing struct or union is not supported yet");
            }
            arg = new_cast(arg, param_ty);
            param_ty = param_ty -> next;
        }

        cur = cur -> next = arg;
        
        if(consume(","))
            continue;
    }

    Node* node = new_node(ND_FUNCCALL);
    node -> funcname = func_name;
    node -> args = head.next;
    node -> ty = ty -> ret_ty;
    return node;   
}