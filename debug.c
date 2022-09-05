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

        case TK_EOF:
            fprintf(debug, "TK_EOF\n");
            return;
    }
}

void display_func(Obj *func){
    fprintf(stderr, "%s: %d parameters, %d local variables\n", func -> name, func -> num_params, func -> locals -> len);
}

static void display_obj(Obj* objp){
    fprintf(debug, "%s  offset: %d\n", objp -> name, objp -> offset);
}


void display_tokenizer_output(Token* head){
    fprintf(debug, "----[Tokenizer Output]----\n");
    for(Token* tp = head; tp; tp = tp -> next){
        display_token(tp);
    }
    return; 
}
