#include "9cc.h"

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
        vp -> data = realloc(vp -> data, vp -> capacity * sizeof(void*));
    }
    vp -> data[vp -> len] = elem; // 0番目の要素はどうなる?
    vp -> len ++;
}

