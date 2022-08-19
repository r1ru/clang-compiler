CFLAGS = -std=c11 -g -static

9cc: 9cc.c

test: 9cc
	./test.sh 

# rmに引数として-fを指定するとエラーメッセージを表示しなくなる。
clean:
	rm -f 9cc *.o *~ tmp* 

# これをしてしなくても実行できるが、カレントディレクトリにtest,cleanという名前のファイルがある場合にうまくいかない。
.PHONY: test clean 