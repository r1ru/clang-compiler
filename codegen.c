#include "9cc.h"

/* 式を左辺値として評価する */
void gen_lval(Node *np) {
    if (np->kind != ND_LVAR)
        error("代入の左辺値が変数ではありません");
    fprintf(STREAM, "\tmov rax, rbp\n");
    fprintf(STREAM, "\tsub rax, %d\n", np->offset);
    fprintf(STREAM, "\tpush rax\n"); /* ローカル変数のアドレスをスタックにpush */
}

/* コードを生成 */
void gen(Node* np){
    switch(np -> kind){
        case ND_NUM:
            fprintf(STREAM, "\tpush %d\n", np -> val); /* ND_NUMなら入力が一つの数値だったということ。*/
            return;
        
        /* これは1+aのように識別子が左辺値以外で使われる場合に使用される */
        case ND_LVAR:
            gen_lval(np); /* 自分自身のアドレス */
            fprintf(STREAM, "\tpop rax\n"); /* ローカル変数のアドレス */
            fprintf(STREAM, "\tmov rax, [rax]\n");
            fprintf(STREAM, "\tpush rax\n"); /* ローカル変数の値をpush */
            return;
        
        case ND_ASSIGN:
            gen_lval(np -> lhs);
            gen(np -> rhs);
            fprintf(STREAM, "\tpop rdi\n"); /* 右辺値 */
            fprintf(STREAM, "\tpop rax\n"); /* ローカル変数のアドレス */
            fprintf(STREAM, "\tmov [rax], rdi\n"); /* ローカル変数へ代入 */
            fprintf(STREAM, "\tpush rdi\n"); /* 代入式の評価結果は代入した値とする。 */
            return;
    }

    /* 左辺と右辺を計算 */
    gen(np -> lhs);
    gen(np -> rhs);

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