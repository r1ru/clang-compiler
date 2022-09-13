#include "9cc.h"

FILE* debug;

static void display_token(Token *tp){

    switch(tp -> kind){
        case TK_IDENT:
            fprintf(debug, "TK_IDENT, ident: %.*s\n", tp -> len , tp -> str);
            return;

        case TK_PUNCT:
            fprintf(debug, "TK_PUNCT, str: %.*s\n", tp -> len, tp -> str);
            return;

        case TK_KEYWORD:
            fprintf(debug, "TK_KEYWORD, str: %.*s\n", tp -> len, tp -> str);
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
            fprintf(debug, "%s[%d]", base_type(ty -> base), ty -> array_len);
            return;

        case TY_FUNC:
            type_info(ty -> ret_ty);
            fprintf(debug, " ( ");
            for(Type *var = ty -> params; var; var = var -> next){
                type_info(var);
                fprintf(debug, " ");
            }
            fprintf(debug, ")");
            return;
        
        case TY_STRUCT:
            fprintf(debug, "struct total: %d bytes\n", ty -> size);
            for(Member *m = ty -> members; m; m = m -> next){
                type_info(m -> ty);
                fprintf(debug, " %.*s offset: %d\n", m -> name -> len,m -> name -> str, m -> offset);
            }
            return;
    }
}

static void display_obj(Obj *obj, bool is_global){
    type_info(obj -> ty);
    fprintf(debug, " %s", obj -> name);
    if(obj -> str){
        fprintf(debug, " \"%s\"", obj -> str);
    }
    if(!is_global && obj -> ty -> kind != TY_STRUCT){
        fprintf(debug, " offset: %d", obj -> offset);
    }else{
        if(obj -> init_data){
            fprintf(debug, "\n");
            int base_size = is_array(obj -> ty) ? obj -> ty -> base -> size : obj -> ty -> size;
           for(InitData *data = obj -> init_data; data; data = data -> next){
                if(!data -> label){
                    if(base_size == 8){
                        fprintf(debug, ".quad %d\n", data -> val);
                    }else if(base_size == 4){
                        fprintf(debug, ".long %d\n", data -> val);
                    }else{
                        fprintf(debug, ".byte %d\n", data -> val);
                    }
                }else{
                    if(base_size == 8){
                        fprintf(debug, ".quad %s+%d\n", data -> label, data -> val);
                    }else if(base_size == 4){
                        fprintf(debug, ".long %s+%d\n", data -> label, data -> val);
                    }else{
                        fprintf(debug, ".byte %s+%d\n", data -> label, data -> val);
                    }
                }
            }
        }
    }
    fprintf(debug, "\n");
}

static void display_func(Obj *func){
    type_info(func -> ty);
    fprintf(debug, " %s:\n", func -> name);
    fprintf(debug, "\t[%d bytes stack]\n", func -> stack_size);
    for(Obj *lvar = func -> locals; lvar; lvar = lvar -> next){
        display_obj(lvar, false);
    }
}

void display_globals(Obj *globals){
    fprintf(debug, "----[Globals]----\n");
    for(Obj *global = globals; global; global = global -> next){
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
