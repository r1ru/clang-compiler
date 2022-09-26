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

/* トークンの名前をバッファに格納してポインタを返す。strndupと同じ動作。 */
static char* get_ident(Token* tok){
    if(tok -> kind != TK_IDENT)
        error_at(tok -> str, "expected an identifier\n");
    char* name = calloc(1, tok -> len + 1); // null終端するため。
    return strncpy(name, tok -> str, tok -> len);
}

static long get_number(Token *tok){
    if(tok -> kind != TK_NUM)
        error_at(tok -> str, "expected a number\n");
    return tok -> val;
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
    sprintf(buf, ".L.%d", idx);
    idx++;
    return buf;
}

static Obj *new_string_literal(Token *tok){
    Type *ty = array_of(ty_char, tok -> len - 1); // ""ぶんだけ引いてnull文字分+1
    Obj *strl = new_gvar(new_unique_name(), ty);
    strl -> str = tok -> str;
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
static Type* func_params(Type *ret_ty);
static Type* type_suffix(Type *ty);
static Type *array_dementions(Type *ty);
static Type* declarator(Type *ty);
static Type *abstract_declarator(Type *ty);
static void function(Type *ty, VarAttr *attr);
static Node *expr_stmt(void);
static Node* stmt(void);
static Node* compound_stmt(void);
static Node* declaration(Type *base);
static Node* expr(void);
static Node* assign(void);
static Node *logor(void);
static Node *logand(void);
static Node *bitor(void);
static Node *bitxor(void);
static Node *bitand(void);
static Node* equality(void);
static Node * relational(void);
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
            function(ty, &attr);
        }
        else{
            new_gvar(get_ident(ty -> name), ty);
            while(!is_equal(token, ";")){
                if(!consume(",")){
                    break;
                }
                ty = declarator(base);
                new_gvar(get_ident(ty -> name), ty);
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

/* function = ";" | "{" compound_stmt */
static void function(Type *ty, VarAttr *attr){
    Obj* func = new_gvar(get_ident(ty -> name), ty);
    func -> is_definition = !consume(";");
    func -> is_static = attr -> is_static;

    if(!func -> is_definition)
        return;
    
    current_fn = func;
    locals = NULL;
    enter_scope(); //仮引数を関数のスコープに入れるため。
    create_param_lvars(ty -> params);
    func -> params = locals;
    expect("{");
    func -> body = compound_stmt();
    func-> locals = locals;
    leave_scope();
    resolve_goto_labels();
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

/* stmt = "return" expr ";" 
        | "if" "(" expr ")" stmt ("else" stmt)?
        | "while" "(" expr ")" stmt
        | "for" "(" expr? ";" expr? ";" expr? ")" stmt 
        | "goto" ident 
        | ident ":" stmt
        | "break" ";"
        | "continue" ";"
        | "{" compound-stmt
        | expr-stmt */
static Node* stmt(void){

    if(consume("return")){
        Node *node = new_node(ND_RET);
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
            node -> init = declaration(base);
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

    if(consume("{")){
        return compound_stmt();
    }

    return expr_stmt();
}

static bool is_typename(Token *tok){
    static char* kw[] = {"void", "char", "short", "int", "long", "void", "struct", "union", "typedef", "_Bool", "enum", "static"};
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


/* decl = declspec declarator ( "," declarator)* ";" */
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
    ty -> members = struct_members();
    ty -> align = 1; 

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

     /* 不完全型なら何もしない */
    if(ty -> size < 0)
        return ty;
    
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

/*  enum-specifier   = ident? "{" enum-list? "}"
                    | ident
    enum-list       = enumerator ("," enumerator)*
    enumerator      = ident ( "=" constant-expression )? */
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

    int val = 0;
    while(!consume("}")){
        char *name = get_ident(token);
        next_token();
        if(consume("=")){
            val = get_number(token);
            next_token();
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
        if(is_equal(token, "typedef") || is_equal(token, "static")){
            if(!attr){
                error_at(token -> str, "storage class specifier is not allowed in this context");
            }
            if(is_equal(token, "typedef"))
                attr -> is_typedef = true;
            else
                attr -> is_static = true;

            if(attr -> is_typedef + attr -> is_static > 1)
                error_at(token -> str, "typedef and static may not be used together\n");
            token = token -> next;
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

/* func-params = (param ("," param)*)? ")"
 param       = type-specifier declarator */
static Type* func_params(Type *ret_ty){
    Type head = {};
    Type *cur = &head;
    if(!is_equal(token, ")")){
        do{
            Type *ty = declspec(NULL); // 仮引数にtypedefは来れない。
            ty = declarator(ty);

            /* array of T を pointer to T に変換する */
            if(ty -> kind == TY_ARRAY){
                Token *name = ty -> name;
                ty = pointer_to(ty -> base);
                ty -> name = name;
            }
            cur = cur -> next = copy_type(ty); // copyしないと上書きされる可能性があるから。
        }while(consume(","));   
    }
    Type *func = func_type(ret_ty);
    func -> params = head.next;
    expect(")");
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

/* array-dementions = num? "}" type-suffix */
static Type *array_dementions(Type *ty){
    if(consume("]")){
        ty = type_suffix(ty);
        return array_of(ty, -1);
    }
    int siz = expect_number();
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

/* declspec declarator ("=" assign)? ("," declarator ("=" assign)?)* ";" */
static Node *declaration(Type *base){
    Node head = {};
    Node *cur = &head;
    while(!consume(";")){
        Type* ty = declarator(base);

        if(ty -> size < 0)
             error_at(ty -> name -> str, "variable hs incomplete type");
        if(is_void(ty))
            error_at(ty -> name -> str, "variable declared void");

        Obj *lvar = new_lvar(get_ident(ty -> name), ty);
        
        if(consume("=")){
            Node *lhs = new_var_node(lvar);
            Node *rhs = assign();
            Node *node = new_binary(ND_ASSIGN, lhs, rhs);
            cur = cur -> next  = new_unary(ND_EXPR_STMT, node);
        }

        if(consume(",")){
            continue;
        }
    }
    Node *node = new_node(ND_BLOCK);
    node -> body = head.next;
    return node;
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

/*  assign = logor (assing_op assign)?
    assing-op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "|=" |"^= | "&=" */
static Node* assign(void){
    Node* node = logor();
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
    if(consume("="))
        node = new_binary(ND_ASSIGN, node, assign());
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
        consume("(");
        Type *ty = typename();
        expect(")");
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

/* postfix  = primary ("[" expr "]" | "." ident | "->" ident | "++" | "--")* */
static Node* postfix(void){
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

    /* そうでなければ数値のはず */
    return new_num_node(expect_number());
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