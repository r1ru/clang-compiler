#include <stdio.h>
#include <stdlib.h>

#define STREAM stdout

int main(const int argc, const char* argv[]){
    fprintf(STREAM, ".intel_syntax noprefix\n");
    fprintf(STREAM, ".global main\n");
    fprintf(STREAM, "main:\n");
    fprintf(STREAM, "\tmov rax, %d\n", atoi(argv[1]));
    fprintf(STREAM, "\tret\n");
    return 0;

}
