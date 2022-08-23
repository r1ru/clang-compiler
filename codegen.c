#include "9cc.h"

static unsigned int llabel_index; // ローカルラベル用のインデックス

/* ローカル変数のアドレスを計算してスタックにpush */
void gen_addr(Node *np) {
    if (np->kind != ND_LVAR)
        error("代入の左辺値が変数ではありません");
    fprintf(STREAM, "\tmov rax, rbp\n");
    fprintf(STREAM, "\tsub rax, %d\n", np->offset);
    fprintf(STREAM, "\tpush rax\n"); /* ローカル変数のアドレスをスタックにpush */
}

void gen_expr(Node* np){
    switch(np -> kind){
        case ND_NUM:
            fprintf(STREAM, "\tpush %d\n", np -> val); /* ND_NUMなら入力が一つの数値だったということ。*/
            return;
        
        /* これは1+aのように識別子が左辺値以外で使われる場合に使用される */
        case ND_LVAR:
            gen_addr(np); /* 自分自身のアドレス */
            fprintf(STREAM, "\tpop rax\n"); /* ローカル変数のアドレス */
            fprintf(STREAM, "\tmov rax, [rax]\n");
            fprintf(STREAM, "\tpush rax\n"); /* ローカル変数の値をpush */
            return;

        case ND_ASSIGN:
            gen_addr(np -> lhs);
            gen_expr(np -> rhs);
            fprintf(STREAM, "\tpop rax\n"); /* 右辺値 */
            fprintf(STREAM, "\tpop rdi\n"); /* ローカル変数のアドレス */
            fprintf(STREAM, "\tmov [rdi], rax\n"); /* ローカル変数へ代入 */
            fprintf(STREAM, "\tpush rax\n"); /* 代入式の評価結果は代入した値とする。 */
            return;
        
        case ND_FUNCCALL:
            fprintf(STREAM, "\tcall %s\n", np -> funcname);
            fprintf(STREAM, "\tpush rax\n"); // 返り値をスタックにpush
            return;
    }

    /* 左辺と右辺を計算 */
    gen_expr(np -> lhs);
    gen_expr(np -> rhs);

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

    fprintf(STREAM, "\tpush rax\n");
}

void gen_stmt(Node* np){
    switch(np -> kind){
    
        case ND_RET:
            gen_expr(np -> expr);
            fprintf(STREAM, "\tpop rax\n");
            fprintf(STREAM, "\tmov rsp, rbp\n");
            fprintf(STREAM, "\tpop rbp\n");
            fprintf(STREAM, "\tret\n"); /* return の右に指定された式の値が返り値になる。*/
            return;

        case ND_IF:
            gen_expr(np -> cond);
            fprintf(STREAM, "\tpop rax\n");
            fprintf(STREAM, "\tcmp rax, 1\n");
            fprintf(STREAM, "\tjne .L%u\n", llabel_index); // 条件式が偽の時はelseに指定されているコードに飛ぶ
            gen_stmt(np -> then); // 条件式が真の時に実行される。
            fprintf(STREAM, "\tjmp .Lend\n");
            fprintf(STREAM, ".L%u:\n", llabel_index);
            if(np -> els){
                gen_stmt(np -> els); // 条件式が偽の時に実行される。
            }
            fprintf(STREAM, ".Lend:\n");
            llabel_index++; // インデックスを更新
            return;

        case ND_WHILE:
            fprintf(STREAM, ".L%u:\n", llabel_index);
            gen_expr(np -> cond);
            fprintf(STREAM, "\tpop rax\n");
            fprintf(STREAM, "\tcmp rax, 1\n");
            fprintf(STREAM, "\tjne .Lend\n"); // 条件式が偽の時は終了
            gen_stmt(np -> body); // 条件式が真の時に実行される。
            fprintf(STREAM, "\tjmp .L%u\n", llabel_index); // 条件式の評価に戻る
            fprintf(STREAM, ".Lend:\n");
            llabel_index++; // インデックスを更新
            return;

        case ND_FOR:
            if(np -> init){
                gen_expr(np -> init);
            }
            fprintf(STREAM, ".L%u:\n", llabel_index);
            if(np -> cond){
                gen_expr(np -> cond);
                fprintf(STREAM, "\tpop rax\n");
                fprintf(STREAM, "\tcmp rax, 1\n");
                fprintf(STREAM, "\tjne .Lend\n"); // 条件式が偽の時は終了

            }
            gen_stmt(np -> body); // bodyは必ずあることが期待されている。
            if(np -> inc){
                gen_expr(np -> inc);
            }
            fprintf(STREAM, "\tjmp .L%u\n", llabel_index); // 条件式の評価に戻る
            fprintf(STREAM, ".Lend:\n");
            llabel_index++; // インデックスを更新
            return;
        
        case ND_BLOCK:
            for(unsigned int i = 0; i < np -> vec -> len; i++){
                gen_stmt(np -> vec -> data[i]);
                fprintf(STREAM, "\tpop rax\n");
            }
            return;

        case ND_EXPR_STMT:
            gen_expr(np -> expr);
            return;   
    }
}

void codegen(void){
    
    /* アセンブリの前半を出力 */
    fprintf(STREAM, ".intel_syntax noprefix\n");
    fprintf(STREAM, ".global main\n");
    fprintf(STREAM, "main:\n");

    /* プロローグ。変数26個分の領域を確保する */
    fprintf(STREAM, "\tpush rbp\n");
    fprintf(STREAM, "\tmov rbp, rsp\n");
    fprintf(STREAM, "\tsub rsp, 208\n"); /* 8 * 26 */

    /* 先頭の文からコード生成 */
    for( int i = 0; code[i]; i++){
        gen_stmt(code[i]);
        fprintf(STREAM, "\tpop rax\n"); /* 式の評価結果がスタックに積まれているはず。*/
    }

    /* エピローグ */
    fprintf(STREAM, "\tmov rsp, rbp\n");
    fprintf(STREAM, "\tpop rbp\n");
    fprintf(STREAM, "\tret\n"); /* 最後の式の評価結果が返り値になる。*/
    
}