#include "9cc.h"

Function* program;

static Function* current_fp; // 現在コードを生成している関数へのポインタ
static unsigned int llabel_index; // ローカルラベル用のインデックス
static char* argreg[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

static void push(void){
    fprintf(STREAM, "\tpush rax\n");
}

static void pop(char* arg){
    fprintf(STREAM, "\tpop %s\n", arg);
}

static void gen_expr(Node* np);

/* アドレスを計算してraxにセット */
static void gen_addr(Node *np) {
    switch(np -> kind){
        case ND_LVAR:
            fprintf(STREAM, "\tlea rax, [rbp -%d]\n", np -> offset); // 変数のアドレスを計算
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
        
        /* これは1+aのように識別子が左辺値以外で使われる場合に使用される */
        case ND_LVAR:
            gen_addr(np); /* 自分自身のアドレス */ 
            fprintf(STREAM, "\tmov rax, [rax]\n"); // ローカル変数の値をraxにいれる。
            return;

        case ND_ASSIGN:
            gen_addr(np -> lhs);
            push();// pushしないと上書きされる可能性がある。
            gen_expr(np -> rhs); // 右辺を計算。
            pop("rdi"); // アドレスをpop
            fprintf(STREAM, "\tmov [rdi], rax\n"); /* ローカル変数へ代入 */
            return;
        
        case ND_FUNCCALL:
            /* 引数があれば */
            if(np -> args){
                int i;
                for(i= np -> args -> len - 1; 0 <= i; i--){
                    gen_expr(np -> args -> data[i]); // 引数を逆順にスタックに積む。こうすると6つ以上の引数をとる関数呼び出を実現するのが簡単になる。
                    push();
                }
                // x86-64では先頭から6つの引数までをレジスタで渡す。
                for(i = 0; i < np -> args -> len || 6 < i; i++){
                    pop(argreg[i]); // レジスタにストア(前から順番に。)
                }
            }
            fprintf(STREAM, "\tcall %s\n", np -> funcname);

            if(np -> args && np -> args -> len > 6){
                fprintf(STREAM, "\tsub rsp, %u\n", 8 * np -> args -> len - 6);
            }
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
    gen_expr(np -> lhs);
    push();
    gen_expr(np -> rhs);
    push();

    fprintf(STREAM, "\tpop rdi\n"); //rhs
    fprintf(STREAM, "\tpop rax\n"); //lhs

    switch(np -> kind){
        case ND_ADD:
        fprintf(STREAM, "\tadd rax, rdi\n");
        break;
    
        case ND_SUB:
            fprintf(STREAM, "\tsub rax, rdi\n");
            break;

        case ND_MUL:
            fprintf(STREAM, "\timul rax, rdi\n");
            break;

        case ND_DIV:
            fprintf(STREAM, "\tcqo\n");
            fprintf(STREAM, "\tidiv rdi\n");
            break;

        case ND_EQ:
            fprintf(STREAM, "\tcmp rax, rdi\n");
            fprintf(STREAM, "\tsete al\n"); /* rflagsから必要なフラグをコピー恐らくZF */
            fprintf(STREAM, "\tmovzb rax, al\n"); /* 上位ビットを0埋め。(eaxへのmov以外、上位ビットは変更されない)*/
            break;

        case ND_NE:
            fprintf(STREAM, "\tcmp rax, rdi\n");
            fprintf(STREAM, "\tsetne al\n");
            fprintf(STREAM, "\tmovzb rax, al\n");
            break;

        case ND_LT:
            fprintf(STREAM, "\tcmp rax, rdi\n");
            fprintf(STREAM, "\tsetl al\n");
            fprintf(STREAM, "\tmovzb rax, al\n");
            break;
        
        case ND_LE:
            fprintf(STREAM, "\tcmp rax, rdi\n");
            fprintf(STREAM, "\tsetle al\n");
            fprintf(STREAM, "\tmovzb rax, al\n");
            break;
    }    
}

static void gen_stmt(Node* np){
    switch(np -> kind){
    
        case ND_RET:
            gen_expr(np -> rhs);
            fprintf(STREAM, "\tjmp .L.end.%s\n", current_fp -> name);
            return;

        case ND_IF:
            gen_expr(np -> cond);
            fprintf(STREAM, "\tcmp rax, 1\n");
            fprintf(STREAM, "\tjne .L.%u\n", llabel_index); // 条件式が偽の時はelseに指定されているコードに飛ぶ
            gen_stmt(np -> then); // 条件式が真の時に実行される。
            fprintf(STREAM, "\tjmp .L.end.%u\n", llabel_index);
            fprintf(STREAM, ".L.%u:\n", llabel_index);
            if(np -> els){
                gen_stmt(np -> els); // 条件式が偽の時に実行される。
            }
            fprintf(STREAM, ".L.end.%u:\n", llabel_index);
            llabel_index++; // インデックスを更新
            return;

        case ND_WHILE:
            fprintf(STREAM, ".L.%u:\n", llabel_index);
            gen_expr(np -> cond);
            fprintf(STREAM, "\tcmp rax, 1\n");
            fprintf(STREAM, "\tjne .L.end.%u\n", llabel_index); // 条件式が偽の時は終了
            gen_stmt(np -> then); // 条件式が真の時に実行される。
            fprintf(STREAM, "\tjmp .L.%u\n", llabel_index); // 条件式の評価に戻る
            fprintf(STREAM, ".L.end.%u:\n", llabel_index);
            llabel_index++; // インデックスを更新
            return;

        case ND_FOR:
            if(np -> init){
                gen_expr(np -> init);
            }
            fprintf(STREAM, ".L.%u:\n", llabel_index);
            if(np -> cond){
                gen_expr(np -> cond);
                fprintf(STREAM, "\tcmp rax, 1\n");
                fprintf(STREAM, "\tjne .L.end.%u\n", llabel_index); // 条件式が偽の時は終了

            }
            gen_stmt(np -> then); // thenは必ずあることが期待されている。
            if(np -> inc){
                gen_expr(np -> inc);
            }
            fprintf(STREAM, "\tjmp .L.%u\n", llabel_index); // 条件式の評価に戻る
            fprintf(STREAM, ".L.end.%u:\n", llabel_index);
            llabel_index++; // インデックスを更新
            return;
        
        case ND_BLOCK:
            for(unsigned int i = 0; i < np -> vec -> len; i++){
                gen_stmt(np -> vec -> data[i]);
            }
            return;

        case ND_EXPR_STMT:
            gen_expr(np -> rhs);
            return;   
    }
}

void codegen(void){
    fprintf(STREAM, ".intel_syntax noprefix\n");
    for (Function *fp = program; fp; fp = fp->next) {
        current_fp = fp;

        /* アセンブリの前半を出力 */
        fprintf(STREAM, ".global %s\n", current_fp -> name);
        fprintf(STREAM, "%s:\n", current_fp -> name);

        /* プロローグ。 */
        fprintf(STREAM, "\tpush rbp\n");
        fprintf(STREAM, "\tmov rbp, rsp\n");
        if(fp -> stacksiz != 0){
            fprintf(STREAM, "\tsub rsp, %u\n", current_fp -> stacksiz);
        }

        int i;
        /* パラメータをスタック領域にコピーする */
        for(i = 0; i < current_fp -> num_params; i++){
            Obj* lvar = current_fp -> locals -> data[i];
            fprintf(STREAM, "\tmov [rbp - %d], %s\n", lvar -> offset, argreg[i]);
        }

        /* コード生成 */
        for(i = 0; i < current_fp -> body -> len; i++){
            gen_stmt(current_fp -> body -> data[i]);
        }

        /* エピローグ */
        fprintf(STREAM, ".L.end.%s:\n", current_fp -> name); // このラベルは関数ごと。
        fprintf(STREAM, "\tmov rsp, rbp\n");
        fprintf(STREAM, "\tpop rbp\n");
        fprintf(STREAM, "\tret\n"); /* 最後の式の評価結果が返り値になる。*/   
    }
}