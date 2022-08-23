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

    codegen();

    /* debug用のファイル記述子をclose */
    if(fclose(debug) == EOF){
        perror("fclose");
        return 1;
    }
    
    return 0;
}