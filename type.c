#include "9cc.h"

Type *ty_int = &(Type){TY_INT};


Type* pointer_to(Type *base){
    Type *ty = calloc(1, sizeof(Type));
    ty -> kind = TY_PTR;
    ty -> ptr_to = base;
    return ty;
}