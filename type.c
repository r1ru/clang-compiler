#include "9cc.h"

Type *ty_long = &(Type){TY_LONG, 8};// 現状pointerの引き算の結果の型にしか使っていない。
Type *ty_int = &(Type){TY_INT, 4};
Type *ty_char =&(Type){TY_CHAR, 1};

Type* pointer_to(Type *base){
    Type *ty = calloc(1, sizeof(Type));
    ty -> kind = TY_PTR;
    ty -> size = 8;
    ty -> base = base;
    return ty;
}

Type* array_of(Type *base, int array_len){
    Type * ty = calloc(1, sizeof(Type));
    ty -> kind = TY_ARRAY;
    ty -> base = base;
    ty -> array_len = array_len;
    ty -> size = base -> size * array_len;
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
    return ty -> kind == TY_INT || ty -> kind == TY_CHAR;
}

bool is_ptr(Type *ty){
    return ty -> base != NULL;
}

bool is_func(Type *ty){
    return ty -> kind == TY_FUNC;
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
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_ASSIGN:
            node -> ty = node ->lhs -> ty;
            return;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_NUM:
        case ND_FUNCCALL:
            node -> ty = ty_int;
            return;
        case ND_VAR:
            node -> ty = node -> var -> ty;
            return;
        case ND_ADDR:
            if(node -> rhs -> ty -> kind == TY_ARRAY)
                node->ty = pointer_to(node -> rhs -> ty -> base);
            node ->ty = pointer_to(node -> rhs -> ty);
            return;
        case ND_DEREF:
            if (!node-> rhs -> ty -> base) //pointer型でなけれなエラー
                error("invalid pointer dereference");
            node -> ty = node -> rhs -> ty -> base;
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
                node -> ty = stmt -> rhs -> ty;
                return;
            }
        }
}
