#include "9cc.h"

static Obj *current_fn; // 現在コードを生成している関数
static char* argreg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char* argreg32[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
static char* argreg16[] = {"di", "si", "dx", "cx", "r8w", "r9w"};
static char* argreg8[] = {"dil", "sil", "dl", "cl", "r8b", "r9b" };

static void gen_expr(Node* node);
static void gen_stmt(Node* node);

static void push(void){
    fprintf(STREAM, "\tpush rax\n");
}

static void pop(char* arg){
    fprintf(STREAM, "\tpop %s\n", arg);
}

static int get_index(void){
    static int index;
    return index++;
}

/* raxに入ってるアドレスにから値を読む。*/
static void load(Type* ty){
    if(ty -> kind == TY_ARRAY){
        return;
    }

    /* sxはsign extendedの略 */
    switch(ty -> size){
        case 1:
            fprintf(STREAM, "\tmovsx eax, BYTE PTR [rax]\n"); 
            return;
        
        case 2:
            fprintf(STREAM, "\tmovsx eax, BYTE PTR [rax]\n"); 
            return;

        case 4:
            fprintf(STREAM, "\tmovsxd rax, [rax]\n");
            return;
        
        default:
            fprintf(STREAM, "\tmov rax, [rax]\n");
            return;
    }
}

/* スタックに積まれているアドレスに値を格納。*/
static void store(Type *ty){
    pop("rdi");
    switch (ty -> size){
        case 1:
            fprintf(STREAM, "\tmov [rdi], al\n");
            return;
        
        case 2:
            fprintf(STREAM, "\tmov [rdi], ax\n");
            return;
        
        case 4:
            fprintf(STREAM, "\tmov [rdi], eax\n");
            return;
        
        default:
            fprintf(STREAM, "\tmov [rdi], rax\n"); 
            return;
    }
}

static void gen_expr(Node* node);

/* アドレスを計算してraxにセット */
static void gen_addr(Node *node) {
    switch(node -> kind){
        case ND_VAR:
            if(node -> var -> is_global){
                fprintf(STREAM, "\tlea rax, %s[rip]\n", node -> var -> name);
                return;
            }
            else{
                fprintf(STREAM, "\tlea rax, [rbp -%d]\n", node -> var -> offset);
                return;
            }
        case ND_DEREF:
            gen_expr(node -> lhs);
            return;
        
        /* x.aはxのアドレス + aのoffset */
        case ND_MEMBER:
            gen_addr(node -> lhs);
            fprintf(STREAM, "\tadd rax, %d\n", node -> member -> offset);
            return;

        case ND_COMMA:
            gen_expr(node -> lhs);
            gen_addr(node -> rhs);
            return;
    }
    error("代入の左辺値が変数ではありません");
}

enum { I8, I16, I32, I64};

static int getTypeId(Type *ty){
    switch(ty -> kind){
        case TY_CHAR:
            return I8;
        
        case TY_SHORT:
            return I16;
        
        case TY_INT:
            return I32;
        
    }
    return I64; // ty_longとかpointer型とか。
}

static char i32i8[] = "movsx eax, al";
static char i32i16[] = "movsx eax, ax";
static char i32i64[] = "movsxd rax, eax";

/*  cast to i8: alが有効
    cast to i16: axが有効
    cast to i64: 現状fromがeaxかalなので普通にeaxをraxに拡張すればいい。*/ 
static char *cast_table[][10] = {
  {NULL,  NULL,   NULL, i32i64}, // i8
  {i32i8, NULL,   NULL, i32i64}, // i16
  {i32i8, i32i16, NULL, i32i64}, // i32
  {i32i8, i32i16, NULL, NULL},   // i64
};

static void cast(Type *from, Type *to){
    if(to -> kind == TY_VOID){
        return; // voidへのキャストは無視
    }
    if(to -> kind == TY_BOOL){
        fprintf(STREAM, "\tcmp rax, 0\n");
        fprintf(STREAM, "\tsetne al\n");
        fprintf(STREAM, "\tmovzx eax, al\n");
        return;
    }
    int t1 = getTypeId(from);
    int t2 = getTypeId(to);
    
    if(cast_table[t1][t2])
        fprintf(STREAM, "\t%s\n", cast_table[t1][t2]);
}

/* 式の評価結果はraxレジスタに格納される。 */
static void gen_expr(Node* node){
    switch(node -> kind){
        case ND_NUM:
            fprintf(STREAM, "\tmov rax, %ld\n", node -> val); /* ND_NUMなら入力が一つの数値だったということ。*/
            return;
        
        case ND_VAR:
        case ND_MEMBER:
            gen_addr(node);
            load(node -> ty);
            return;
        
        case ND_NEG:
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tneg rax\n");
            return;

        case ND_ASSIGN:
            gen_addr(node -> lhs);
            push();// pushしないと上書きされる可能性がある。
            gen_expr(node -> rhs); // 右辺を計算。
            store(node -> ty);
            return;

        case ND_FUNCCALL:{
            /* parser側でひと工夫しているので先頭からpushするだけで逆順になる。*/
            int nargs = 0;
            for(Node *arg = node -> args; arg; arg = arg -> next){
                gen_expr(arg);
                nargs++;
                push();
            }
            /* x86-64では先頭から6つの引数までをレジスタで渡す。 */
            for(int i = nargs - 1;  0 <= i; i--){
                pop(argreg64[i]);
            }
            fprintf(STREAM, "\tmov rax, 0\n"); // 浮動小数点の引数の個数
            fprintf(STREAM, "\tcall %s\n", node -> funcname);
            return;
        }
        
        case ND_ADDR:
            gen_addr(node -> lhs);
            return;
        
        case ND_DEREF:
            gen_expr(node -> lhs);
            load(node -> ty);
            return;

        case ND_NOT:
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tsete al\n");
            fprintf(STREAM, "\tmovzx rax, al\n");
            return;
        
        case ND_BITNOT:
            gen_expr(node ->lhs);
            fprintf(STREAM, "\tnot rax\n");
            return;

        case ND_STMT_EXPR:
            for(Node *stmt = node -> body; stmt; stmt = stmt -> next){
                gen_stmt(stmt);
            }
            return;
        
        case ND_CAST:
            gen_expr(node -> lhs);
            cast(node -> lhs -> ty, node ->ty);
            return;
        
        case ND_COMMA:
            gen_expr(node -> lhs);
            gen_expr(node -> rhs);
            return;

        case ND_LOGOR:{
            int idx = get_index();
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tjne .L.true.%d\n", idx);
            gen_expr(node -> rhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tjne .L.true.%d\n", idx);
            fprintf(STREAM, "\tmov rax, 0\n");
            fprintf(STREAM, "\tjmp .L.end.%d\n", idx);
            fprintf(STREAM, ".L.true.%d:\n", idx);
            fprintf(STREAM, "\tmov rax, 1\n");
            fprintf(STREAM, ".L.end.%d:\n", idx);
            return;
        }
        
        case ND_LOGAND:{
            int idx = get_index();
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.false.%d\n", idx);
            gen_expr(node -> rhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.false.%d\n", idx);
            fprintf(STREAM, "\tmov rax, 1\n");
            fprintf(STREAM, "\tjmp .L.end.%d\n", idx);
            fprintf(STREAM, ".L.false.%d:\n", idx);
            fprintf(STREAM, "\tmov rax, 0\n");
            fprintf(STREAM, ".L.end.%d:\n", idx);
            return;
        }
    }

    /* 左辺と右辺を計算してスタックに保存 */
    gen_expr(node -> rhs);
    push();
    gen_expr(node -> lhs);
    pop("rdi");

    switch(node -> kind){
        case ND_ADD:
            fprintf(STREAM, "\tadd rax, rdi\n");
            return;
    
        case ND_SUB:
            fprintf(STREAM, "\tsub rax, rdi\n");
            return;

        case ND_MUL:
            fprintf(STREAM, "\timul rax, rdi\n");
            return;

        case ND_DIV:
        case ND_MOD:
            fprintf(STREAM, "\tcqo\n");
            fprintf(STREAM, "\tidiv rdi\n");
            if(node -> kind == ND_MOD)
                fprintf(STREAM, "\tmov rax, rdx\n");
            return;

        case ND_BITOR:
            fprintf(STREAM, "\tor rax, rdi\n");
            return;
        
        case ND_BITXOR:
            fprintf(STREAM, "\txor rax, rdi\n");
            return;
        
        case ND_BITAND:
            fprintf(STREAM, "\tand rax, rdi\n");
            return;
        
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            fprintf(STREAM, "\tcmp rax, rdi\n");
        if(node -> kind == ND_EQ){
            fprintf(STREAM, "\tsete al\n");
        }
        else if(node -> kind == ND_NE){
            fprintf(STREAM, "\tsetne al\n");
        }
        else if(node -> kind == ND_LT){
            fprintf(STREAM, "\tsetl al\n");
        }
        else if(node -> kind == ND_LE){
            fprintf(STREAM, "\tsetle al\n");
        }
        fprintf(STREAM, "\tmovzb rax, al\n");
        return;
    }    
}

static void gen_stmt(Node* node){
    switch(node -> kind){
    
        case ND_RET:
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tjmp .L.end.%s\n", current_fn -> name);
            return;

        case ND_IF:{
            int idx = get_index();
            gen_expr(node -> cond);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.else.%u\n", idx); // 条件式が偽の時はelseに指定されているコードに飛ぶ
            gen_stmt(node -> then); // 条件式が真の時に実行される。
            fprintf(STREAM, "\tjmp .L.end.%u\n", idx);
            fprintf(STREAM, ".L.else.%u:\n", idx);
            if(node -> els){
                gen_stmt(node -> els); // 条件式が偽の時に実行される。
            }
            fprintf(STREAM, ".L.end.%u:\n", idx);
            return;
        }

        case ND_FOR:{
            int idx = get_index();
            if(node -> init){
                gen_stmt(node -> init);
            }
            fprintf(STREAM, ".L.begin.%u:\n", idx);
            if(node -> cond){
                gen_expr(node -> cond);
                fprintf(STREAM, "\tcmp rax, 0\n");
                fprintf(STREAM, "\tje %s\n", node -> brk_label); // 条件式が偽の時は終了

            }
            gen_stmt(node -> then); // thenは必ずあることが期待されている。
            fprintf(STREAM, "%s:\n", node -> cont_label);
            if(node -> inc){
                gen_expr(node -> inc);
            }
            fprintf(STREAM, "\tjmp .L.begin.%u\n", idx); // 条件式の評価に戻る
            fprintf(STREAM, "%s:\n", node -> brk_label);
            return;
        }

        case ND_GOTO:
            fprintf(STREAM, "\tjmp %s\n", node -> unique_label);
            return;
        
        case ND_LABEL:
            fprintf(STREAM, "%s:\n", node -> unique_label);
            gen_stmt(node -> lhs);
            return;
        
        case ND_SWITCH:
            gen_expr(node -> cond);
            for(Node *n = node -> case_next; n; n = n -> case_next){
                fprintf(STREAM, "\tcmp rax, %ld\n", n -> val);
                fprintf(STREAM, "\tje %s\n", n -> unique_label);
            }
            if(node -> default_case)
                fprintf(STREAM, "jmp %s\n", node -> default_case -> unique_label);
            // 該当するcaseがなかった時
            fprintf(STREAM, "\tjmp %s\n", node -> brk_label);

            gen_stmt(node -> then);
            fprintf(STREAM, "%s:\n", node -> brk_label);
            return;
        
        case ND_CASE:
            fprintf(STREAM, "%s:", node -> unique_label);
            gen_stmt(node -> lhs);
            return;
        
        case ND_BLOCK:
            for(Node *stmt = node -> body; stmt; stmt = stmt -> next){
                gen_stmt(stmt);
            }
            return;

        case ND_EXPR_STMT:
            gen_expr(node -> lhs);
            return;   
    }
}

static void store_arg(int i, int offset, unsigned int size){
    switch(size){
        case 1:
            fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg8[i]);
            return;
        
        case 2:
            fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg16[i]);
            return;
        
        case 4:
            fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg32[i]);
            return;
        
        default:
            fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg64[i]);
            return;
    }
}

int align_to(int offset, int align){
    return (offset + align - 1) / align * align;
}

static void assign_lvar_offsets(Obj *globals){
    for(Obj *fn = globals; fn; fn = fn -> next){
        if(!is_func(fn -> ty)){
            continue;
        }

        int offset = 0;
        for(Obj *lvar = fn -> locals; lvar; lvar = lvar ->next){
            offset += lvar -> ty -> size;
            lvar -> offset = offset;
        }
        fn -> stack_size = align_to(offset, 16);
    }
}

static void emit_data(Obj *globals){
    fprintf(STREAM, ".data\n");
    for(Obj *gvar = globals; gvar; gvar = gvar -> next){
        int remain = gvar -> ty -> size;
        int base_size = is_array(gvar -> ty) ? gvar -> ty -> base -> size : gvar -> ty -> size;
        if(is_func(gvar -> ty)){
            continue;
        }
        fprintf(STREAM, ".global %s\n", gvar -> name);
        fprintf(STREAM, "%s:\n", gvar -> name); 
        
        if(is_array(gvar -> ty) && gvar -> str){
            for(int i = 0; i < gvar -> ty -> array_len; i++){
                fprintf(STREAM, "\t.byte %d\n", gvar -> str[i]);
            }
            continue;
        }
        
        /* 配列か普通の変数 */
        for(InitData *data = gvar -> init_data; data; data = data -> next){
            if(!data -> label){
                if(base_size == 8){
                    fprintf(STREAM, "\t.quad %ld\n", data -> val);
                }else if(base_size == 4){
                    fprintf(STREAM, "\t.long %ld\n", data -> val);
                }else{
                    fprintf(STREAM, "\t.byte %ld\n", data -> val);
                }
            }else{
                if(base_size == 8){
                    fprintf(STREAM, "\t.quad %s+%ld\n", data -> label, data -> val);
                }else if(base_size == 4){
                    fprintf(STREAM, "\t.long %s+%ld\n", data -> label, data -> val);
                }else{
                    fprintf(STREAM, "\t.byte %s+%ld\n", data -> label, data -> val);
                }
            }
            remain -= base_size;
        }
        if(remain != 0)
            fprintf(STREAM, "\t.zero %d\n", remain);
    }
}

static void emit_text(Obj *globals){
    fprintf(STREAM, ".text\n");
    for(Obj *fn = globals; fn; fn = fn -> next){
        if(!is_func(fn -> ty) || !fn -> is_definition){
            continue;
        }
        current_fn = fn;
       
        if(fn -> is_static)
            fprintf(STREAM, ".local %s\n", fn -> name);
        else
            fprintf(STREAM, ".global %s\n", fn -> name);
        
        fprintf(STREAM, "%s:\n", fn -> name);

        /* プロローグ。 */
        fprintf(STREAM, "\tpush rbp\n");
        fprintf(STREAM, "\tmov rbp, rsp\n");
        if(fn -> stack_size != 0){
            fprintf(STREAM, "\tsub rsp, %u\n", fn -> stack_size);
        }

        int i = 0;
        /* パラメータをスタック領域にコピー */
        for(Obj *var = fn -> params; var; var = var -> next){
            store_arg(i, var -> offset, var -> ty -> size);
            i++;
        }
        /* コード生成 */
        gen_stmt(fn -> body);

        /* エピローグ */
        fprintf(STREAM, ".L.end.%s:\n", fn -> name); // このラベルは関数ごと。
        fprintf(STREAM, "\tmov rsp, rbp\n");
        fprintf(STREAM, "\tpop rbp\n");
        fprintf(STREAM, "\tret\n"); /* 最後の式の評価結果が返り値になる。*/   
    }
}

void codegen(Obj *globals){
    fprintf(STREAM, ".intel_syntax noprefix\n");
    assign_lvar_offsets(globals);
    emit_data(globals);
    emit_text(globals);
}
