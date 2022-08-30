#include "9cc.h"

Type *ty_int = &(Type){TY_INT};

Type* pointer_to(Type *base){
    Type *ty = calloc(1, sizeof(Type));
    ty -> kind = TY_PTR;
    ty -> base = base;
    return ty;
}

bool is_integer(Node* np){
    if(np -> kind == ND_NUM){
        return true;
    }
    if(np -> kind == ND_LVAR){
        return np -> var -> ty -> kind == TY_INT;
    }
    return false;
}

bool is_ptr(Node* np){
    if(np -> kind ==ND_LVAR){
         return np -> var -> ty -> kind == TY_PTR;
    }
    if(np -> kind == ND_ADDR){
        return true;
    }
    return false;
}
