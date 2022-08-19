#include <stdio.h>
#include <stdlib.h>

#define STREAM stdout
#define ERROR stderr

int main(int argc, char* argv[]){
    if(argc != 2){
        fprintf(ERROR, "引数の個数が正しくありません\n");
        return EXIT_FAILURE;
    }
    char * p = argv[1];

    fprintf(STREAM, ".intel_syntax noprefix\n");
    fprintf(STREAM, ".global main\n");
    fprintf(STREAM, "main:\n");
    fprintf(STREAM, "\tmov rax, %ld\n", strtol(p, &p, 10));

    while(*p){
        if(*p == '+'){
            p++;
            fprintf(STREAM, "\tadd rax, %ld\n", strtol(p, &p, 10));
            continue;
        }else if(*p == '-'){
            p++;
            fprintf(STREAM, "\tsub rax, %ld\n", strtol(p, &p, 10));
            continue;
        }else{
            fprintf(ERROR, "予期しない入力です\n");
            return 1;
        }
    }

    fprintf(STREAM, "\tret\n");
    return 0;

}
