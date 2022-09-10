#include "test.h"

int main() {
    ASSERT(3, ({ int x; x=3; *&x; }));
    ASSERT(3, ({ int x; x=3; int *y; y=&x; int **z; z=&y; **z; }));
    ASSERT(5, ({ int x; x=3; int y; y=5; *(&x+1); }));
    ASSERT(3, ({ int x; x=3; int y; y=5; *(&y-1); }));
    ASSERT(5, ({ int x; x=3; int y; y=5; *(&x -(-1)); }));
    ASSERT(5, ({ int x; x=3; int *y; y=&x; *y=5; x; }));
    ASSERT(7, ({ int x; x=3; int y; y=5; *(&x+1)=7; y; }));
    ASSERT(7, ({ int x; x=3; int y; y=5; *(&y-2+1)=7; x; }));
    ASSERT(5, ({ int x; x=3; (&x+2)-&x+3; }));

    ASSERT(3, ({ int x[2]; int *y; y=&x; *y=3; *x; }));

    ASSERT(3, ({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *x; }));
    ASSERT(4, ({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *(x+1); }));
    ASSERT(5, ({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *(x+2); }));

    ASSERT(3, ({ int x[3]; *x=3; x[1]=4; x[2]=5; *x; }));
    ASSERT(4, ({ int x[3]; *x=3; x[1]=4; x[2]=5; *(x+1); }));
    ASSERT(5, ({ int x[3]; *x=3; x[1]=4; x[2]=5; *(x+2); }));
    ASSERT(5, ({ int x[3]; *x=3; x[1]=4; x[2]=5; *(x+2); }));
    ASSERT(5, ({ int x[3]; *x=3; x[1]=4; 2[x]=5; *(x+2); }));
    
    printf("OK\n");
    return 0;
}