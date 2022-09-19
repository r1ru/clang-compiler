#include "test.h"

char g1 = 3;
int g2 = 5 + 2 * 3;

char g3[3] = "abc";
char g4[3] = "ab";
char g5[] = "abc";

int g6, g7 = 1;
int g8 = 1, g9[3] = {1,2}; 

int main() {
    ASSERT(1, ({ int x[3]={1,2,3}; x[0]; }));
    ASSERT(2, ({ int x[3]={1,2,3}; x[1]; }));
    ASSERT(3, ({ int x[3]={1,2,3}; x[2]; }));
    ASSERT(0, ({ int x[3]={1,2}; x[2];}));
    ASSERT(2, ({ int x[]={1,2}; x[1];}));
    ASSERT(2, ({ int y=2, x[2]={1,2}; x[1];}));
    ASSERT(2, ({ int y=2, x[2]={1,2}; y;}));
    ASSERT(97, ({ char a[] = "abc"; a[0];}));
    ASSERT(98, ({ char a[] = "abc"; a[1];}));
    ASSERT(99, ({ char a[] = "abc"; a[2];}));
    ASSERT(0, ({ char a[3] = "ab"; a[2];}));
    ASSERT(97, ({ char *p = "abc"; p[0];}));

    ASSERT(3, ({g1;}));
    ASSERT(11, ({g2;}));
    ASSERT(97, ({g3[0];}));
    ASSERT(98, ({g3[1];}));
    ASSERT(99, ({g3[2];}));
    ASSERT(0, ({g4[2];}));
    ASSERT(97, ({g5[0];}));
    ASSERT(98, ({g5[1];}));
    ASSERT(99, ({g5[2];}));
    ASSERT(1, ({g6 + g7;}));
    ASSERT(1, ({g8 + g9[2];}));
    printf("OK\n");
  return 0;
}
