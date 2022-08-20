#include "9cc.h"

/* コードを生成 */
void gen(Node* np){

    /* ND_KINDなら入力が一つの数値だったということ。*/
    if(np -> kind == ND_NUM){
        fprintf(STREAM, "\tpush %d\n", np -> val);
        return;
    }

    /* 左辺と右辺を計算 */
    gen(np -> lhs);
    gen(np -> rhs);

    fprintf(STREAM, "\tpop rdi\n"); //rhs
    fprintf(STREAM, "\tpop rax\n"); //lhs

    switch( np -> kind){

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