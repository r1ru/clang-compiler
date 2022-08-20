#include "9cc.h"

int main(int argc, char* argv[]){
    if(argc != 2){
        fprintf(ERROR, "引数の個数が正しくありません\n");
        return EXIT_FAILURE;
    }

    /* 入力を保存 */
    input = argv[1];

    /* tokenize */
    token = tokenize();

    /* 構文解析 */
    Node *np  = expr();

     /* アセンブリの前半を出力 */
    fprintf(STREAM, ".intel_syntax noprefix\n");
    fprintf(STREAM, ".global main\n");
    fprintf(STREAM, "main:\n");

    /* コード生成 */
    gen(np);

    /* 結果をpop */
    fprintf(STREAM, "\tpop rax\n");
    fprintf(STREAM, "\tret\n");
    
    return 0;

}