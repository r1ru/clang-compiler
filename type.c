#include "9cc.h"

Type *ty_int = &(Type){TY_INT, 4};

Type* pointer_to(Type *base){
    Type *ty = calloc(1, sizeof(Type));
    ty -> kind = TY_PTR;
    ty -> size = 8;
    ty -> base = base;
    return ty;
}

bool is_integer(Type *ty){
    return ty -> kind == TY_INT;
}

bool is_ptr(Type *ty){
    return ty -> kind == TY_PTR;
}

void add_type(Node *node) {
    if (!node) //有効な値がない可能性があるため。
        return;

    switch (node->kind) {
        /* expression */
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
            add_type(node -> rhs);
            add_type(node -> lhs);
            node -> ty = node ->lhs -> ty;
            return;
        
        case ND_ASSIGN:
            add_type(node -> rhs);
            add_type(node -> lhs);
            node -> ty = node -> lhs ->ty;
            return;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            add_type(node -> rhs);
            add_type(node -> lhs);
        case ND_NUM:
        case ND_FUNCCALL:
            node -> ty = ty_int; // TODO: ここを直す。
            return;
        case ND_LVAR:
            node -> ty = node -> var -> ty;
            return;
        case ND_ADDR:
            add_type(node -> rhs);
            node ->ty = pointer_to(node -> rhs -> ty);
            return;
        case ND_DEREF:
            add_type(node -> rhs);
            if (!node-> rhs -> ty -> base) //pointer型でなけれなエラー
                error("invalid pointer dereference");
            node -> ty = node -> rhs -> ty ->base;
            return;
        /* stmt */
        case ND_RET:
            add_type(node -> rhs);
            return;
        case ND_IF:
            add_type(node -> cond);
            add_type(node -> then);
            add_type(node -> els);
            return;
        case ND_WHILE:
            add_type(node -> cond);
            add_type(node -> then);
            return;
        case ND_FOR:
            add_type(node -> init);
            add_type(node -> cond);
            add_type(node -> inc);
            add_type(node -> then);
            return;
        case ND_EXPR_STMT:
            add_type(node -> rhs);
            return;
        case ND_BLOCK:
            for(unsigned int i = 0; i < node -> body -> len; i++){
                Node* n = node -> body -> data[i];
                add_type(n);
            }   
            return;
        }
}
