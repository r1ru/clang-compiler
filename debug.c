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

static void display_obj(Obj* objp){
    fprintf(debug, "%s  offset: %d\n", objp -> name, objp -> offset);
}

static void display_func(Function* fp){
    fprintf(debug, "%s  ", fp -> name);
    fprintf(debug, "%lu parameters  ", fp -> num_params);
    fprintf(debug, "%u local variables\n", fp -> locals -> len);
    int i;
    for(i = 0; i < fp -> locals -> len; i++){
        Obj* lvar = fp -> locals -> data[i];
        display_obj(lvar);
    }
}

void display_tokenizer_output(Token* head){
    fprintf(debug, "----[Tokenizer Output]----\n");
    for(Token* tp = head; tp; tp = tp -> next){
        display_token(tp);
    }
    return; 
}

void display_parser_output(Function* head){
    fprintf(debug, "----[Parser Output]----\n");
    for(Function* fp = head; fp; fp = fp -> next){
        display_func(fp);
    }
    return; 
}
