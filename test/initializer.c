#include "test.h"

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
    printf("OK\n");
  return 0;
}