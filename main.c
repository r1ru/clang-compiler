#include "9cc.h"

FILE* debug;

int main(int argc, char* argv[]){
    if(argc != 2){
        fprintf(ERROR, "引数の個数が正しくありません\n");
        return EXIT_FAILURE;
    }

    /* debugメッセージ出力用のファイルをオープン */
    debug = fopen("debug", "w");
    if(debug == NULL){
        perror("fopen");
        return 1;
    }

    /* 入力を保存 */
    input = argv[1];

    /* tokenize */
    tokenize();

    /* 構文解析 */
    program();

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
        gen(code[i]);
        fprintf(STREAM, "\tpop rax\n"); /* 式の評価結果がスタックに積まれているはず。*/
    }

    /* エピローグ */
    fprintf(STREAM, "\tmov rsp, rbp\n");
    fprintf(STREAM, "\tpop rbp\n");
    fprintf(STREAM, "\tret\n"); /* 最後の式の評価結果が返り値になる。*/

    /* debug用のファイル記述子をclose */
    if(fclose(debug) == EOF){
        perror("fclose");
        return 1;
    }
    
    return 0;
}