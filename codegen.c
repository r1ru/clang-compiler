#include "9cc.h"

Vector* program;

static Obj *current_func;
static unsigned int llabel_index; // ローカルラベル用のインデックス
static char* argreg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char* argreg32[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};

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
    if(ty -> size == 4){
        fprintf(STREAM, "\tmov eax, [rax]\n");
    }
    else{
        fprintf(STREAM, "\tmov rax, [rax]\n");
    }
}

/* スタックに積まれているアドレスに値を格納。*/
static void store(Type *ty){
    pop("rdi");
    if(ty -> size == 4){
        fprintf(STREAM, "\tmov [rdi], eax\n");
    }
    else{
       fprintf(STREAM, "\tmov [rdi], rax\n"); 
    }
}

static void gen_expr(Node* np);

/* アドレスを計算してraxにセット */
static void gen_addr(Node *np) {
    switch(np -> kind){
        case ND_LVAR:
            fprintf(STREAM, "\tlea rax, [rbp -%d]\n", np -> var -> offset); // 変数のアドレスを計算
            return;
        case ND_DEREF:
            gen_expr(np -> rhs);
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
        
        case ND_LVAR:
            gen_addr(np);
            load(np -> ty);
            return;

        case ND_ASSIGN:
            gen_addr(np -> lhs);
            push();// pushしないと上書きされる可能性がある。
            gen_expr(np -> rhs); // 右辺を計算。
            store(np -> ty);
            return;

        case ND_FUNCCALL:
            /* 引数があれば */
            if(np -> args){
                int i; // ここはintにしないとバグる。0 <= -1の評価が必要になるため。
                for(i= np -> args -> len - 1; 0 <= i; i--){
                    gen_expr(np -> args -> data[i]); // 引数を逆順にスタックに積む。こうすると6つ以上の引数をとる関数呼び出を実現するのが簡単になる。
                    push();
                }
                // x86-64では先頭から6つの引数までをレジスタで渡す。TODO: 16btyeアラインメントする
                for(i = 0; i < np -> args -> len && i < 6; i++){
                    pop(argreg64[i]); // レジスタにストア(前から順番に。)
                }
            }
            fprintf(STREAM, "\tcall %s\n", np -> funcname);
            return;
        
        case ND_ADDR:
            gen_addr(np -> rhs);
            return;
        
        case ND_DEREF:
            gen_expr(np -> rhs); //なぜ式?
            fprintf(STREAM, "\tmov rax, [rax]\n");
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
            fprintf(STREAM, "\tjmp .L.end.%s\n", current_func -> name);
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
             for(unsigned int i = 0; i < np -> body -> len; i++){
                gen_stmt(np -> body -> data[i]);
            }
            return;

        case ND_EXPR_STMT:
            gen_expr(np -> rhs);
            return;   
    }
}

static void store_arg(int i, int offset, unsigned int size){
    if(size == 4){
        fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg32[i]);
    }
    else{
         fprintf(STREAM, "\tmov [rbp - %d], %s\n", offset, argreg64[i]);
    }
}

static int align_to(int offset, int align){
    return (offset + align - 1) / align * align;
}

static void assign_lvar_offsets(Obj* func){
    int offset = 0;
    for(int i = 0; i < func -> locals -> len; i++){
        Obj* lvar = func -> locals -> data[i];
        offset += lvar -> ty -> size;
        lvar -> offset = offset;
    }
    func -> stack_size = align_to(offset, 16);
}

void codegen(void){
    fprintf(STREAM, ".intel_syntax noprefix\n");
    for(int i = 0; i < program -> len; i++){
        current_func = program -> data[i];
        assign_lvar_offsets(current_func);
        display_func(current_func);
        /* アセンブリの前半を出力 */
        fprintf(STREAM, ".global %s\n", current_func -> name);
        fprintf(STREAM, "%s:\n", current_func -> name);

        /* プロローグ。 */
        fprintf(STREAM, "\tpush rbp\n");
        fprintf(STREAM, "\tmov rbp, rsp\n");
        if(current_func -> stack_size != 0){
            fprintf(STREAM, "\tsub rsp, %u\n", current_func -> stack_size);
        }

        unsigned int i;
        /* パラメータをスタック領域にコピー */
        for(i = 0; i < current_func -> num_params && i < 6; i++){
            Obj* lvar = current_func -> locals -> data[i];
            store_arg(i, lvar -> offset, lvar -> ty -> size);
        }

        /* コード生成 */
        gen_stmt(current_func -> body);

        /* エピローグ */
        fprintf(STREAM, ".L.end.%s:\n", current_func -> name); // このラベルは関数ごと。
        fprintf(STREAM, "\tmov rsp, rbp\n");
        fprintf(STREAM, "\tpop rbp\n");
        fprintf(STREAM, "\tret\n"); /* 最後の式の評価結果が返り値になる。*/   
    }
}