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

    /* tokenize */
    tokenize(argv[1]);

    /* 構文解析 */
    parse();

    codegen();

    /* debug用のファイル記述子をclose */
    if(fclose(debug) == EOF){
        perror("fclose");
        return 1;
    }
    
    return 0;
}