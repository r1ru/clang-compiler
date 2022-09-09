CFLAGS = -std=c11 -g -static -Wall #makeの組み込みルールによって認識される変数。*/
SRCS=$(wildcard *.c) #wildcardはmakeが提供している関数で引数にマッチするファイル名に展開される。*/
OBJS=$(SRCS:.c=.o) #置換ルールを適用。.cを.oに置換している。

TEST_SRCS=$(wildcard test/*.c)
TESTS = $(TEST_SRCS:.c=)

9cc: $(OBJS)
	$(CC) -o 9cc $(OBJS) $(LDFLAGS)

$(OBJS): 9cc.h #9cc.hが更新されたときにすべてを再コンパイルするため

test/%: 9cc test/%.c 
	$(CC) -E -P test/$*.c > test/tmpc 
	./9cc test/tmpc > test/tmps
	$(CC) -o $@ -xassembler test/tmps -xc test/common

test: $(TESTS)
	for i in $^; do echo $$i; $$i || exit 1; done

	

# rmに引数として-fを指定するとエラーメッセージを表示しなくなる。
clean:
	rm -f 9cc *.o *~ tmp* 
	rm -f test/tmp.c test/tmp.s

# これをしてしなくても実行できるが、カレントディレクトリにtest,cleanという名前のファイルがある場合にうまくいかない。
.PHONY: test clean 