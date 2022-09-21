#include "9cc.h"

Type *ty_long = &(Type){TY_LONG, 8, 8};
Type *ty_int = &(Type){TY_INT, 4, 4};
Type *ty_short = &(Type){TY_SHORT, 2, 2};
Type *ty_char =&(Type){TY_CHAR, 1, 1};
Type *ty_void =&(Type){TY_VOID, 1, 1};
Type *ty_bool =&(Type){TY_BOOL, 1, 1};

Type *new_type(TypeKind kind, int size, int align){
    Type *ty = calloc(1, sizeof(Type));
    ty -> kind = kind;
    ty -> size = size;
    ty -> align = align;
    return ty;
}

Type* pointer_to(Type *base){
    Type *ty = new_type(TY_PTR, 8, 8);
    ty -> base = base;
    return ty;
}

Type* array_of(Type *base, int array_len){
    Type * ty = new_type(TY_ARRAY, base -> size * array_len, base -> align);
    ty -> base = base;
    ty -> array_len = array_len;
    return ty;
}

Type* func_type(Type *ret_ty){
    Type *ty = calloc(1, sizeof(Type));
    ty -> kind = TY_FUNC;
    ty -> ret_ty = ret_ty;
    return ty;
}

Type* copy_type(Type *ty){
    Type *ret = calloc(1, sizeof(Type));
    *ret = *ty;
    return ret;
}

bool is_integer(Type *ty){
    TypeKind k = ty -> kind;
    return k == TY_INT || k == TY_CHAR || k == TY_LONG || k == TY_SHORT || k == TY_BOOL;
}

bool is_ptr(Type *ty){
    return ty -> base != NULL;
}

bool is_void(Type *ty){
    return ty -> kind == TY_VOID;
}

bool is_func(Type *ty){
    return ty -> kind == TY_FUNC;
}

bool is_array(Type *ty){
    return ty -> kind == TY_ARRAY;
}

bool is_struct(Type *ty){
    return ty -> kind == TY_STRUCT;
}

bool is_union(Type *ty){
    return ty -> kind == TY_UNION;
}

static Type *get_common_type(Type *t1, Type *t2){
    if(is_ptr(t1)){
        return t1;
    }
    if(t1 -> size == 8 || t2 -> size == 8){
        return ty_long;
    }
    return ty_int;
}

static void usual_arith_conv(Node **lhs, Node **rhs){
    Type *ty = get_common_type((*lhs) -> ty, (*rhs) -> ty);
    *lhs = new_cast(*lhs, ty);
    *rhs = new_cast(*rhs, ty);
}

void add_type(Node *node) {
    /* 有効な値でないか、Nodeが既に型付けされている場合は何もしない。上書きを防ぐため。*/
    if (!node || node -> ty) 
        return;

    add_type(node -> rhs);
    add_type(node -> lhs);

    add_type(node -> cond);
    add_type(node -> then);
    add_type(node -> els);
    add_type(node -> init);
    add_type(node -> inc);

    /* ND_BLOCK or ND_STMT_EXPR */
    for(Node *stmt = node -> body; stmt; stmt = stmt -> next){
        add_type(stmt);
    }   

    /* ND_FUNCALL */
    for(Node *arg = node -> args; arg; arg = arg -> next){
        add_type(arg);
    }

    switch (node -> kind) {
        case ND_NUM:
            node -> ty = node -> val == (int)node -> val ? ty_int : ty_long;
            return;
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
            usual_arith_conv(&node -> lhs, &node -> rhs);
            node -> ty = node -> lhs -> ty;
            return;
        case ND_NEG:{
            Type *ty = get_common_type(ty_int, node -> lhs ->ty);
            node -> lhs = new_cast(node -> lhs, ty);
            node -> ty = ty;
            return;
        }

        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            usual_arith_conv(&node -> lhs, &node ->rhs);
            node -> ty = ty_int;
            return;
        /* parser側でtypeを追加しているのにこれを入れる必要はあるのか。*/
        case ND_FUNCCALL:
            node -> ty = ty_long;
            return;
        case ND_VAR:
            node -> ty = node -> var -> ty;
            return;
        case ND_ADDR:
            if(node -> lhs -> ty -> kind == TY_ARRAY)
                node -> ty = pointer_to(node -> lhs -> ty -> base);
            node -> ty = pointer_to(node -> lhs -> ty);
            return;
        case ND_DEREF:
            if (!node-> lhs -> ty -> base) //pointer型でなけれなエラー
                error("invalid pointer dereference");
            if(node -> lhs -> ty -> base -> kind == TY_VOID)
                error("dereferencing 'void *' pointer ");
            node -> ty = node -> lhs -> ty -> base;
            return;
       
        case ND_ASSIGN:
            if(is_array(node -> lhs ->ty)){
                error("not an lvalue");
            }   
            if(!is_struct(node -> lhs -> ty))
                node -> rhs = new_cast(node -> rhs, node -> lhs -> ty);
            node -> ty = node -> lhs -> ty;
            return;
        case ND_STMT_EXPR:
            if(node -> body){
                Node *stmt = node -> body;
                while(stmt -> next){
                    stmt = stmt -> next;
                }
                if(stmt -> kind != ND_EXPR_STMT){
                    error("statement expression returning void is not supported"); // TODO: ここを改良
                }
                node -> ty = stmt -> lhs -> ty;
                return;
            }
            
        case ND_MEMBER:
            node -> ty = node -> member -> ty;
        }
}
