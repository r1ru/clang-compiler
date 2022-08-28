#!/bin/bash
# テストスクリプトをデバッグしたいときはbashに引数として-xを指定するとよい。

cat <<EOF | gcc -xc -c -o tmp2.o -
int ret3() { return 3; }
int ret5() { return 5; }
int add(int x, int y) { return x+y; }
int sub(int x, int y) { return x-y; }
int add6(int a, int b, int c, int d, int e, int f) {
  return a+b+c+d+e+f;
}
EOF

assert() {
  expected="$1"
  input="$2"

  ./9cc "$input" > tmp.s
  cc -o tmp tmp.s tmp2.o
  ./tmp
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

assert 0 'int main(){0;}'
assert 42 'int main(){42;}'
assert 41 'int main(){12 + 34 - 5; }'
assert 47 'int main(){5+6*7;}'
assert 15 'int main(){5*(9-6);}'
assert 4 'int main(){(3+5)/2;}'

assert 10 'int main(){-10+20;}'
assert 10 'int main(){- -10;}'
assert 10 'int main(){- - +10;}'

assert 0 'int main(){0==1;}'
assert 1 'int main(){42==42;}'
assert 1 'int main(){0!=1;}'
assert 0 'int main(){42!=42;}'

assert 1 'int main(){0<1;}'
assert 0 'int main(){1<1;}'
assert 0 'int main(){2<1;}'
assert 1 'int main(){0<=1;}'
assert 1 'int main(){1<=1;}'
assert 0 'int main(){2<=1;}'

assert 1 'int main(){1>0;}'
assert 0 'int main(){1>1;}'
assert 0 'int main(){1>2;}'
assert 1 'int main(){1>=0;}'
assert 1 'int main(){1>=1;}'
assert 0 'int main(){1>=2;}'

assert 3 'int main(){inta; a=1+2;}'
assert 3 'int main(){int a; int b; a=1; b=2; a+b;}'

assert 6 'int main(){int foo; int bar; foo = 1;bar = 2 + 3; foo + bar;}'

assert 5 'int main(){return 5;}'
assert 14 'int main(){int a; int b; a = 3; b = 5 * 6 - 8; return a + b / 2;}'

assert 3 'int main(){int i; i=2; if(i == 2) i+1;}'
assert 4 'int main(){if (0) return 3; else return 4;}'
assert 3 'int main(){if (1) return 3; else return 4;}'

assert 10 'int main(){int i; i=0; while(i<10) i=i+1; return i;}'

assert 55 'int main(){int i; int j; i=0; j=0; for (i=0; i<=10; i=i+1) j=i+j; return j;}'
assert 3 'int main(){for (;;) return 3; return 5;}'

assert 0 'int main(){ return 0; }'
assert 42 'int main(){ return 42; }'
assert 21 'int main(){ return 5+20-4; }'
assert 41 'int main(){ return  12 + 34 - 5 ; }'
assert 47 'int main(){ return 5+6*7; }'
assert 15 'int main(){ return 5*(9-6); }'
assert 4 'int main(){ return (3+5)/2; }'
assert 10 'int main(){ return -10+20; }'
assert 10 'int main(){ return - -10; }'
assert 10 'int main(){ return - - +10; }'

assert 0 'int main(){ return 0==1; }'
assert 1 'int main(){ return 42==42; }'
assert 1 'int main(){ return 0!=1; }'
assert 0 'int main(){ return 42!=42; }'

assert 1 'int main(){ return 0<1; }'
assert 0 'int main(){ return 1<1; }'
assert 0 'int main(){ return 2<1; }'
assert 1 'int main(){ return 0<=1; }'
assert 1 'int main(){ return 1<=1; }'
assert 0 'int main(){ return 2<=1; }'

assert 1 'int main(){ return 1>0; }'
assert 0 'int main(){ return 1>1; }'
assert 0 'int main(){ return 1>2; }'
assert 1 'int main(){ return 1>=0; }'
assert 1 'int main(){ return 1>=1; }'
assert 0 'int main(){ return 1>=2; }'

assert 3 'int main(){ int a; a=3; return a; }'
assert 8 'int main(){ int a; int z; a=3; z=5; return a+z; }'
assert 6 'int main(){ int a; int b; a=b=3; return a+b; }'
assert 3 'int main(){ int foo; foo=3; return foo; }'
assert 8 'int main(){ int foo123; int bar; foo123=3; bar=5; return foo123+bar; }'
assert 1 'int main(){ return 1; 2; 3; }'
assert 2 'int main(){ 1; return 2; 3; }'
assert 3 'int main(){ 1; 2; return 3; }'

assert 3 'int main(){ {1; {2;} return 3;} }'

assert 3 'int main(){return ret3();}'
assert 5 'int main(){return ret5();}'

assert 8 'int main(){ return add(3, 5); }'
assert 2 'int main(){ return sub(5, 3); }'
assert 21 'int main(){ return add6(1,2,3,4,5,6); }'
assert 66 'int main(){ return add6(1,2,add6(3,4,5,6,7,8),9,10,11); }'
assert 136 'int main(){ return add6(1,2,add6(3,add6(4,5,6,7,8,9),10,11,12,13),14,15,16); }'

assert 7 'int main() { return add2(3,4); } int add2(int x, int y) { return x+y; }'
assert 1 'int main() { return sub2(4,3); } int sub2(int x, int y) { return x-y; }'
assert 55 'int main() { return fib(9); } int fib(int x) { if (x<=1) return 1; return fib(x-1) + fib(x-2); }'

assert 3 'int main(){ intx; x=3; return *&x; }'
assert 3 'int main(){ int x; int y; int z; x=3; y=&x; z=&y; return **z; }'
assert 5 'int main(){ int x; int y; x=3; y=5; return *(&x-8); }'
assert 3 'int main(){ int x; int y; x=3; y=5; return *(&y+8); }'
assert 5 'int main(){ int x; int y; x=3; y=&x; *y=5; return x; }'
assert 7 'int main(){ int x; int y; x=3; y=5; *(&x-8)=7; return y; }'
assert 7 'int main(){ int x; int y; x=3; y=5; *(&y+8)=7; return x; }'

assert 28 'int main(){ return add8(0,1,2,3,4,5,6,7);} int add8(int a, int b, int c, int d, int e, int f, int g, int h){ return a + b + c + d+ e + f + g + h;}'

assert 3 'int main(){int x; int *y; y = &x; *y = 3; return x;}'
echo OK