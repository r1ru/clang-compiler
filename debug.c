#include "9cc.h"

FILE* debug;

static void display_token(Token *tp){

    switch(tp -> kind){

        case TK_RESERVED:
            fprintf(debug, "TK_RESERVED, str: %.*s\n", tp -> len, tp -> str);
            return;

        case TK_IDENT:
            fprintf(debug, "TK_IDENT, ident: %.*s\n", tp -> len , tp -> str);
            return;

        case TK_NUM:
            fprintf(debug, "TK_NUM, val: %d\n", tp -> val);
            return;

        case TK_STR:   
            fprintf(debug, "TK_STR, val: %.*s\n", tp -> len, tp -> str);
            return; 

        case TK_EOF:
            fprintf(debug, "TK_EOF\n");
            return;
    }
}

static char* base_type(Type *ty){
    switch(ty -> kind){
        case TY_INT:
            return "int";
        case TY_CHAR:
            return "char";
        default:
            error("unknown type");
    }
}

static void putnchar(char c, int num){
    for(int i =0; i < num; i++){
        fprintf(debug, "%c", c);
    }
}

static void type_info(Type *ty){
    int i = 0;
    Type *t;
    switch(ty -> kind){
        case TY_INT:
            fprintf(debug, "%s", base_type(ty));
            return;
        
        case TY_CHAR:
            fprintf(debug, "%s", base_type(ty));
            return;

        case TY_PTR:
            for(t = ty; t -> base; t = t -> base){
                i++;
            }
            fprintf(debug, "%s", base_type(t));
            putnchar('*', i);
            return;

        case TY_ARRAY:
            fprintf(debug, "%s[%d]", base_type(ty -> base), ty -> size / ty -> base -> size);
            return;

        case TY_FUNC:
            type_info(ty -> ret_ty);
            fprintf(debug, " ( ");
            if(ty -> params){
                for(i = 0; i < ty -> params -> len; i++){
                    t = ty -> params -> data[i];
                    type_info(t);
                    fprintf(debug, " ");
                }
            }
            fprintf(debug, ")");
            return;
    }
}

static void display_obj(Obj *obj, bool is_global){
    type_info(obj -> ty);
    fprintf(debug, " %s", obj -> name);
    if(obj -> init_data){
        fprintf(debug, " init_data: \"%s\"", obj -> init_data);
    }
    if(!is_global){
        fprintf(debug, " offset: %d", obj -> offset);
    }
    fprintf(debug, "\n");
}

static void display_func(Obj *func){
    type_info(func -> ty);
    fprintf(debug, " %s:\n", func -> name);
    fprintf(debug, "\t[%d local variables, %d bytes stack]\n", func -> locals -> len, func -> stack_size);
    for(int i =0; i < func -> locals -> len; i++){
        Obj *lvar = func -> locals -> data[i];
        fprintf(debug, "\t");
        display_obj(lvar, false);
    }
}

void display_globals(Vector *globals){
    fprintf(debug, "----[Globals]----\n");
    for(int i = 0; i < globals -> len; i++){
        Obj *global = globals -> data[i];
        if(is_func(global -> ty)){
            display_func(global);
            continue;;
        }
        display_obj(global, true);
    }
}

void check_tokenizer_output(Token* head){
    fprintf(debug, "----[Tokenizer Output]----\n");
    for(Token* tp = head; tp; tp = tp -> next){
        display_token(tp);
    }
    fprintf(debug, "\n");
    return; 
}
