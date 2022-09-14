#include "9cc.h"

static char *read_file(char *path){
    FILE *fp = fopen(path, "r");
    if(!fp){
        error("cannot open %s: %s", path, strerror(errno));
    }
    
    /* ファイルの最後まで移動 */
    if(fseek(fp, 0, SEEK_END) == -1){
        error("%s: fseek: %s", path, strerror(errno));
    }

    size_t size = ftell(fp);

    /* ファイルの先頭に移動 */
    if(fseek(fp, 0, SEEK_SET) == -1){
        error("%s: fseek: %s", path, strerror(errno));
    }

    char *buf = calloc(1, size + 2); // \0\n用に+2
    fread(buf, size, 1, fp);

    /* ファイルが\n\0で終わっている様にする。*/
    if(size == 0 || buf[size - 1] != '\n'){
        buf[size] = '\n';
    }

    fclose(fp);
    return buf;
}

int main(int argc, char* argv[]){
    if(argc != 2){
        fprintf(ERROR, "引数の個数が正しくありません\n");
        return EXIT_FAILURE;
    }

    /* ファイルから入力を読み込む */
    char *buf = read_file(argv[1]);
    
    /* tokenize */
    tokenize(argv[1], buf);

    /* 構文解析 */
    Obj *program = parse();

    codegen(program);
    
    return 0;
}