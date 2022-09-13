#include "9cc.h"

static Obj *current_fn; // 現在コードを生成している関数
static unsigned int llabel_index; // ローカルラベル用のインデックス
static char* argreg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char* argreg32[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
static char* argreg8[] = {"dil", "sil", "dl", "cl", "r8b", "r9b" };

static void gen_expr(Node* np);
static void gen_stmt(Node* np);

static void push(void){
    fprintf(STREAM, "\tpush rax\n");
}

static void pop(char* arg){
    fprintf(STREAM, "\tpop %s\n", arg);
}

/* raxに入ってるアドレスにから値を読む。*/
static void load(Type* ty){
    if(ty -> kind == TY_ARRAY){
        return;
    }
    if(ty -> size == 1){
        fprintf(STREAM, "\tmovzx eax, BYTE PTR [rax]\n"); 
        return;
    }    
    if(ty -> size == 4){
        fprintf(STREAM, "\tmov eax, [rax]\n");
        return;
    }
    fprintf(STREAM, "\tmov rax, [rax]\n");
}

/* スタックに積まれているアドレスに値を格納。*/
static void store(Type *ty){
    pop("rdi");
    if(ty -> size == 1){
        fprintf(STREAM, "\tmov [rdi], al\n");
        return;
    }
    if(ty -> size == 4){
        fprintf(STREAM, "\tmov [rdi], eax\n");
        return;
    }
    fprintf(STREAM, "\tmov [rdi], rax\n"); 
}

static void gen_expr(Node* np);

/* アドレスを計算してraxにセット */
static void gen_addr(Node *np) {
    switch(np -> kind){
        case ND_VAR:
            if(np -> var -> is_global){
                fprintf(STREAM, "\tlea rax, %s[rip]\n", np -> var -> name);
                return;
            }
            else{
                fprintf(STREAM, "\tlea rax, [rbp -%d]\n", np -> var -> offset);
                return;
            }
        case ND_DEREF:
            gen_expr(np -> rhs);
            return;
        
        /* x.aはxのアドレス + aのoffset */
        case ND_MEMBER:
            gen_addr(np -> lhs);
            fprintf(STREAM, "\tadd rax, %d\n", np -> member -> offset);
            return;
    }
    error("代入の左辺値が変数ではありません");
}

/* 式の評価結果はraxレジスタに格納される。 */
static void gen_expr(Node* np){
    switch(np -> kind){
        case ND_NUM:
            fprintf(STREAM, "\tmov rax, %d\n", np -> val); /* ND_NUMなら入力が一つの数値だったということ。*/
            return;
        
        case ND_VAR:
        case ND_MEMBER:
            gen_addr(np);
            load(np -> ty);
            return;

        case ND_ASSIGN:
            gen_addr(np -> lhs);
            push();// pushしないと上書きされる可能性がある。
            gen_expr(np -> rhs); // 右辺を計算。
            store(np -> ty);
            return;

        case ND_FUNCCALL:{
            /* parser側でひと工夫しているので先頭からpushするだけで逆順になる。*/
            int nargs = 0;
            for(Node *arg = np -> args; arg; arg = arg -> next){
                gen_expr(arg);
                nargs++;
                push();
            }
            /* x86-64では先頭から6つの引数までをレジスタで渡す。 */
            for(int i=0; i < nargs && i < 6; i++){
                pop(argreg64[i]);
            }
            fprintf(STREAM, "\tmov rax, 0\n"); // 浮動小数点の引数の個数
            fprintf(STREAM, "\tcall %s\n", np -> funcname);
            return;
        }
        
        case ND_ADDR:
            gen_addr(np -> rhs);
            return;
        
        case ND_DEREF:
            gen_expr(np -> rhs);
            load(np -> ty);
            return;

        case ND_STMT_EXPR:
            for(Node *stmt = np -> body; stmt; stmt = stmt -> next){
                gen_stmt(stmt);
            }
            return;

    }

    /* 左辺と右辺を計算してスタックに保存 */
    gen_expr(np -> rhs);
    push();
    gen_expr(np -> lhs);
    pop("rdi");

    switch(np -> kind){
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
            fprintf(STREAM, "\tcqo\n");
            fprintf(STREAM, "\tidiv rdi\n");
            return;

        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            fprintf(STREAM, "\tcmp rax, rdi\n");
        if(np -> kind == ND_EQ){
            fprintf(STREAM, "\tsete al\n");
        }
        else if(np -> kind == ND_NE){
            fprintf(STREAM, "\tsetne al\n");
        }
        else if(np -> kind == ND_LT){
            fprintf(STREAM, "\tsetl al\n");
        }
        else if(np -> kind == ND_LE){
            fprintf(STREAM, "\tsetle al\n");
        }
        fprintf(STREAM, "\tmovzb rax, al\n");
        return;
    }    
}

static void gen_stmt(Node* np){
    switch(np -> kind){
    
        case ND_RET:
            gen_expr(np -> rhs);
            fprintf(STREAM, "\tjmp .L.end.%s\n", current_fn -> name);
            return;

        case ND_IF:
            gen_expr(np -> cond);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.else.%u\n", llabel_index); // 条件式が偽の時はelseに指定されているコードに飛ぶ
            gen_stmt(np -> then); // 条件式が真の時に実行される。
            fprintf(STREAM, "\tjmp .L.end.%u\n", llabel_index);
            fprintf(STREAM, ".L.else.%u:\n", llabel_index);
            if(np -> els){
                gen_stmt(np -> els); // 条件式が偽の時に実行される。
            }
            fprintf(STREAM, ".L.end.%u:\n", llabel_index);
            llabel_index++; // インデックスを更新
            return;

        case ND_WHILE:
            fprintf(STREAM, ".L.begin.%u:\n", llabel_index);
            gen_expr(np -> cond);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.end.%u\n", llabel_index); // 条件式が偽の時は終了
            gen_stmt(np -> then); // 条件式が真の時に実行される。
            fprintf(STREAM, "\tjmp .L.begin.%u\n", llabel_index); // 条件式の評価に戻る
            fprintf(STREAM, ".L.end.%u:\n", llabel_index);
            llabel_index++; // インデックスを更新
            return;

        case ND_FOR:
            if(np -> init){
                gen_expr(np -> init);
            }
            fprintf(STREAM, ".L.begin.%u:\n", llabel_index);
            if(np -> cond){
                gen_expr(np -> cond);
                fprintf(STREAM, "\tcmp rax, 0\n");
                fprintf(STREAM, "\tje .L.end.%u\n", llabel_index); // 条件式が偽の時は終了

            }
            gen_stmt(np -> then); // thenは必ずあることが期待されている。
            if(np -> inc){
                gen_expr(np -> inc);
            }
            fprintf(STREAM, "\tjmp .L.begin.%u\n", llabel_index); // 条件式の評価に戻る
            fprintf(STREAM, ".L.end.%u:\n", llabel_index);
            llabel_index++; // インデックスを更新
            return;
        
        case ND_BLOCK:
            for(Node *stmt = np -> body; stmt; stmt = stmt -> next){
                gen_stmt(stmt);
            }
            return;

        case ND_EXPR_STMT:
            gen_expr(np -> rhs);
            return;   
    }
}

static void store_arg(int i, int offset, unsigned int size){
    if(size == 1){
        fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg8[i]);
        return;
    }
    if(size == 4){
        fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg32[i]);
        return;
    }
    fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg64[i]);
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
            fprintf(STREAM, "\t.ascii \"%s\"\n", gvar -> str);
            remain -= strlen(gvar -> str);
            goto end;
        }
        
        /* 配列か普通の変数 */
        for(InitData *data = gvar -> init_data; data; data = data -> next){
            if(!data -> label){
                if(base_size == 8){
                    fprintf(STREAM, "\t.quad %d\n", data -> val);
                }else if(base_size == 4){
                    fprintf(STREAM, "\t.long %d\n", data -> val);
                }else{
                    fprintf(STREAM, "\t.byte %d\n", data -> val);
                }
            }else{
                if(base_size == 8){
                    fprintf(STREAM, "\t.quad %s+%d\n", data -> label, data -> val);
                }else if(base_size == 4){
                    fprintf(STREAM, "\t.long %s+%d\n", data -> label, data -> val);
                }else{
                    fprintf(STREAM, "\t.byte %s+%d\n", data -> label, data -> val);
                }
            }
            remain -= base_size;
        }
        end:
            if(remain != 0)
                fprintf(STREAM, "\t.zero %d\n", remain);
    }
}

static void emit_text(Obj *globals){
    fprintf(STREAM, ".text\n");
    for(Obj *fn = globals; fn; fn = fn -> next){
        if(!is_func(fn -> ty)){
            continue;
        }
        current_fn = fn;
        /* アセンブリの前半を出力 */
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
    //display_globals(globals);
    emit_data(globals);
    emit_text(globals);
}
