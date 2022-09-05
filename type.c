#include "9cc.h"

Type *ty_int = &(Type){TY_INT, 4};

Type* pointer_to(Type *base){
    Type *ty = calloc(1, sizeof(Type));
    ty -> kind = TY_PTR;
    ty -> size = 8;
    ty -> base = base;
    return ty;
}

Type* array_of(Type *base, int len){
    Type * ty = calloc(1, sizeof(Type));
    ty -> kind = TY_ARRAY;
    ty -> base = base;
    ty -> size = base -> size * len;
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
    return ty -> kind == TY_INT;
}

bool is_ptr(Type *ty){
    return ty -> base != NULL;
}

void add_type(Node *node) {
    if (!node) //有効な値がない可能性があるため。
        return;

    add_type(node -> rhs);
    add_type(node -> lhs);

    add_type(node -> cond);
    add_type(node -> then);
    add_type(node -> els);
    add_type(node -> init);
    add_type(node -> inc);

    switch (node->kind) {
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
            node -> ty = ty_int; // TODO: ここを直す。
            return;
        case ND_LVAR:
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
        case ND_BLOCK:
            for(unsigned int i = 0; i < node -> body -> len; i++){
                Node* n = node -> body -> data[i];
                add_type(n);
            }   
            return;
        }
}
