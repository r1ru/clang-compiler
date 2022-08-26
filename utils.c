#include "9cc.h"

/* エラー表示用の関数 */
void error(char *fmt, ...){
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

Vector* new_vec(void){
    Vector* vp = malloc(sizeof(Vector));
    vp -> data = malloc(16 * sizeof(void*));
    vp -> capacity = 16;
    vp -> len = 0;
    return vp;
}

void vec_push(Vector* vp, void* elem){
    if(vp -> len == vp -> capacity){
        // 容量がいっぱいなら2倍に拡張。
        vp -> capacity *= 2; 
        vp -> data = realloc(vp -> data, 16 * sizeof(void*));
    }
    vp -> data[vp -> len++] = elem; // 0番目の要素はどうなる?
}

