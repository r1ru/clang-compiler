#include "9cc.h"

static Obj *current_fn; // 現在コードを生成している関数
static int depth; 

static char* argreg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char* argreg32[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
static char* argreg16[] = {"di", "si", "dx", "cx", "r8w", "r9w"};
static char* argreg8[] = {"dil", "sil", "dl", "cl", "r8b", "r9b" };

static void gen_expr(Node* node);
static void gen_stmt(Node* node);

static void push(void){
    fprintf(STREAM, "\tpush rax\n");
    depth++;
}

static void pop(char* arg){
    fprintf(STREAM, "\tpop %s\n", arg);
    depth--;
}

static int get_index(void){
    static int index;
    return index++;
}

/* raxに入ってるアドレスにから値を読む。*/
static void load(Type* ty){
    if(ty -> kind == TY_ARRAY || ty -> kind == TY_STRUCT || ty -> kind == TY_UNION){
        return;
    }

    char *inst = ty -> is_unsigned ? "movzx" : "movsx";
    
    /* sxはsign extendedの略 */
    switch(ty -> size){
        case 1:
            fprintf(STREAM, "\t%s eax, BYTE PTR [rax]\n", inst); 
            return;
        
        case 2:
            fprintf(STREAM, "\t%s eax, WORD PTR [rax]\n", inst); 
            return;

        case 4:
            fprintf(STREAM, "\t%s rax, [rax]\n", "movsxd");
            return;
        
        default:
            fprintf(STREAM, "\tmov rax, [rax]\n");
            return;
    }
}

/* スタックに積まれているアドレスに値を格納。*/
static void store(Type *ty){
    pop("rdi");
    if(ty -> kind == TY_STRUCT || ty -> kind == TY_UNION){
        // 1byteずつコピーする
        for(int i = 0; i < ty -> size; i++){
            fprintf(STREAM, "\tmov r8b, [rax + %d]\n", i);
            fprintf(STREAM, "\tmov [rdi + %d], r8b\n", i);
        }
        return;
    }
    switch (ty -> size){
        case 1:
            fprintf(STREAM, "\tmov [rdi], al\n");
            return;
        
        case 2:
            fprintf(STREAM, "\tmov [rdi], ax\n");
            return;
        
        case 4:
            fprintf(STREAM, "\tmov [rdi], eax\n");
            return;
        
        default:
            fprintf(STREAM, "\tmov [rdi], rax\n"); 
            return;
    }
}

static void gen_expr(Node* node);

/* アドレスを計算してraxにセット */
static void gen_addr(Node *node) {
    switch(node -> kind){
        case ND_VAR:
            if(node -> var -> is_global){
                fprintf(STREAM, "\tlea rax, %s[rip]\n", node -> var -> name);
                return;
            }
            else{
                fprintf(STREAM, "\tlea rax, [rbp + %d]\n", node -> var -> offset);
                return;
            }
        case ND_DEREF:
            gen_expr(node -> lhs);
            return;
        
        /* x.aはxのアドレス + aのoffset */
        case ND_MEMBER:
            gen_addr(node -> lhs);
            fprintf(STREAM, "\tadd rax, %d\n", node -> member -> offset);
            return;

        case ND_COMMA:
            gen_expr(node -> lhs);
            gen_addr(node -> rhs);
            return;
    }
    error("代入の左辺値が変数ではありません");
}


static void cmp_zero(Type *ty){
    if(is_integer(ty) && ty -> size <= 4)
        fprintf(STREAM, "\tcmp eax, 0\n");
    else 
        fprintf(STREAM, "\tcmp rax, 0\n");
}

enum { I8, I16, I32, I64, U8, U16, U32, U64};

static int getTypeId(Type *ty){
    switch(ty -> kind){
        case TY_CHAR:
            return ty -> is_unsigned ? U8 : I8;
        
        case TY_SHORT:
            return ty -> is_unsigned ? U16 : I16;
        
        case TY_INT:
            return ty -> is_unsigned ? U32 : I32;
        
        case TY_LONG:
            return ty -> is_unsigned ? U64 : I64;
        
    }
    return U64;
}

// ex) i32i8: from I32 to I8
static char i32i8[] = "movsx eax, al";
static char i32u8[] = "movzx eax, al";
static char i32i16[] = "movsx eax, ax";
static char i32u16[] = "movzx eax, ax";
static char i32i64[] = "movsxd rax, eax";
static char u32i64[] = "mov eax, eax";

static char *cast_table[][10] = {
    // i8   i16     i32    i64      u8      u16      u32    u64
    {NULL,  NULL,   NULL,  i32i64,  i32u8,  i32u16,  NULL,  i32i64}, // i8
    {i32i8, NULL,   NULL,  i32i64,  i32u8,  i32u16,  NULL,  i32i64}, // i16
    {i32i8, i32i16, NULL,  i32i64,  i32u8,  i32u16,  NULL,  i32i64}, // i32
    {i32i8, i32i16, NULL,  NULL,    i32u8,  i32u16,  NULL,  NULL},   // i64
    {i32i8, NULL,   NULL,  i32i64,   NULL,  NULL,    NULL,  i32i64}, // u8
    {i32i8, i32i16, NULL,  i32i64,   i32u8, NULL,    NULL,  i32i64}, // u16
    {i32i8, i32i16, NULL,  u32i64,   i32u8, i32u16,  NULL,  u32i64}, // u32
    {i32i8, i32i16, NULL,  NULL,     i32u8, i32u16,  NULL,  NULL},   // u64

};

static void cast(Type *from, Type *to){
    if(to -> kind == TY_VOID){
        return; // voidへのキャストは無視
    }
    if(to -> kind == TY_BOOL){
        cmp_zero(from);
        fprintf(STREAM, "\tsetne al\n");
        fprintf(STREAM, "\tmovzx eax, al\n");
        return;
    }
    int t1 = getTypeId(from);
    int t2 = getTypeId(to);
    
    if(cast_table[t1][t2])
        fprintf(STREAM, "\t%s\n", cast_table[t1][t2]);
}

/* 式の評価結果はraxレジスタに格納される。 */
static void gen_expr(Node* node){
    switch(node -> kind){
        case ND_NULL_EXPR:
            return;
        
        case ND_NUM:
            fprintf(STREAM, "\tmov rax, %ld\n", node -> val); /* ND_NUMなら入力が一つの数値だったということ。*/
            return;
        
        case ND_VAR:
        case ND_MEMBER:
            gen_addr(node);
            load(node -> ty);
            return;
        
        case ND_NEG:
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tneg rax\n");
            return;

        case ND_ASSIGN:
            gen_addr(node -> lhs);
            push();// pushしないと上書きされる可能性がある。
            gen_expr(node -> rhs); // 右辺を計算。
            store(node -> ty);
            return;

        case ND_FUNCCALL:{
            /* parser側でひと工夫しているので先頭からpushするだけで逆順になる。*/
            int nargs = 0;
            for(Node *arg = node -> args; arg; arg = arg -> next){
                gen_expr(arg);
                nargs++;
                push();
            }
            /* x86-64では先頭から6つの引数までをレジスタで渡す。 */
            for(int i = nargs - 1;  0 <= i; i--){
                pop(argreg64[i]);
            }
            fprintf(STREAM, "\tmov rax, 0\n"); // 浮動小数点の引数の個数

            // alignment
            if(depth % 2 == 0)
                fprintf(STREAM, "\tcall %s\n", node -> funcname);
            else{
                fprintf(STREAM, "\tsub rsp, 8\n");
                fprintf(STREAM, "\tcall %s\n", node -> funcname);
                fprintf(STREAM, "\tadd rsp, 8\n");
            }

            switch(node -> ty -> kind){
                case TY_BOOL:
                    fprintf(STREAM, "\tmovzx eax, al\n");
                    return;
                
                case TY_CHAR:
                    if(node -> ty -> is_unsigned)
                        fprintf(STREAM, "\tmovzx eax, al\n");
                    else
                        fprintf(STREAM, "\tmovsx eax, al\n");
                    return;

                case TY_SHORT:
                    if(node -> ty -> is_unsigned)
                        fprintf(STREAM, "\tmovzx eax, ax\n");
                    else 
                        fprintf(STREAM, "\tmovsx eax, ax\n");
                    return;
            }
            
            return;
        }
        
        case ND_ADDR:
            gen_addr(node -> lhs);
            return;
        
        case ND_DEREF:
            gen_expr(node -> lhs);
            load(node -> ty);
            return;

        case ND_NOT:
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tsete al\n");
            fprintf(STREAM, "\tmovzx rax, al\n");
            return;
        
        case ND_BITNOT:
            gen_expr(node ->lhs);
            fprintf(STREAM, "\tnot rax\n");
            return;

        case ND_STMT_EXPR:
            for(Node *stmt = node -> body; stmt; stmt = stmt -> next){
                gen_stmt(stmt);
            }
            return;
        
        case ND_CAST:
            gen_expr(node -> lhs);
            cast(node -> lhs -> ty, node ->ty);
            return;

        case ND_MEMZERO:
            // rep stosb 命令はmemset(rdi, al, rcx)と同じ
            fprintf(STREAM, "\tmov rcx, %d\n", node -> var -> ty -> size);
            fprintf(STREAM, "\tlea rdi, [rbp + %d]\n", node -> var -> offset);
            fprintf(STREAM, "\tmov eax, 0\n");
            fprintf(STREAM, "\trep stosb\n");
            return;

        case ND_COND:{
            int idx = get_index();
            gen_expr(node -> cond);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.else.%d\n", idx);
            gen_expr(node -> then);
            fprintf(STREAM, "\tjmp .L.end.%d\n", idx);
            fprintf(STREAM, ".L.else.%d:\n", idx);
            gen_expr(node -> els);
            fprintf(STREAM, ".L.end.%d:\n", idx);
            return;
        }
        
        case ND_COMMA:
            gen_expr(node -> lhs);
            gen_expr(node -> rhs);
            return;

        case ND_LOGOR:{
            int idx = get_index();
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tjne .L.true.%d\n", idx);
            gen_expr(node -> rhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tjne .L.true.%d\n", idx);
            fprintf(STREAM, "\tmov rax, 0\n");
            fprintf(STREAM, "\tjmp .L.end.%d\n", idx);
            fprintf(STREAM, ".L.true.%d:\n", idx);
            fprintf(STREAM, "\tmov rax, 1\n");
            fprintf(STREAM, ".L.end.%d:\n", idx);
            return;
        }
        
        case ND_LOGAND:{
            int idx = get_index();
            gen_expr(node -> lhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.false.%d\n", idx);
            gen_expr(node -> rhs);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.false.%d\n", idx);
            fprintf(STREAM, "\tmov rax, 1\n");
            fprintf(STREAM, "\tjmp .L.end.%d\n", idx);
            fprintf(STREAM, ".L.false.%d:\n", idx);
            fprintf(STREAM, "\tmov rax, 0\n");
            fprintf(STREAM, ".L.end.%d:\n", idx);
            return;
        }
    }

    /* 左辺と右辺を計算してスタックに保存 */
    gen_expr(node -> rhs);
    push();
    gen_expr(node -> lhs);
    pop("rdi");

    char *ax, *di, *dx;

    if(node -> lhs -> ty -> kind == TY_LONG || node -> lhs -> ty -> base){
        ax = "rax";
        di = "rdi";
        dx = "rdx";
    }else{
        ax = "eax";
        di = "edi";
        dx = "edx";
    }

    switch(node -> kind){
        case ND_ADD:
            fprintf(STREAM, "\tadd %s, %s\n", ax, di);
            return;
    
        case ND_SUB:
            fprintf(STREAM, "\tsub %s, %s\n", ax, di);
            return;

        case ND_MUL:
            fprintf(STREAM, "\timul %s, %s\n", ax, di);
            return;

        case ND_DIV:
        case ND_MOD:
            if(node -> lhs -> ty -> is_unsigned){
                fprintf(STREAM, "\tmov %s, 0\n", dx); //　上位bit0埋め
                fprintf(STREAM, "\tdiv %s\n", di);
            }else{
                if(node -> lhs -> ty -> size == 8)
                    fprintf(STREAM, "\tcqo\n");
                else 
                    fprintf(STREAM, "\tcdq\n");
                fprintf(STREAM, "\tidiv %s\n", di);
            }
            if(node -> kind == ND_MOD)
                fprintf(STREAM, "\tmov rax, rdx\n");
            return;

        case ND_BITOR:
            fprintf(STREAM, "\tor rax, rdi\n");
            return;
        
        case ND_BITXOR:
            fprintf(STREAM, "\txor rax, rdi\n");
            return;
        
        case ND_BITAND:
            fprintf(STREAM, "\tand rax, rdi\n");
            return;

        case ND_SHL:
            fprintf(STREAM, "\tmov rcx, rdi\n");
            fprintf(STREAM, "\tshl rax, cl\n");
            return;
        
        case ND_SHR:
            fprintf(STREAM, "\tmov rcx, rdi\n");
            if(node -> lhs -> ty -> is_unsigned)
                fprintf(STREAM, "\tshr %s, cl\n", ax);
            else
                fprintf(STREAM, "\tsar %s, cl\n", ax);
            return;
        
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            fprintf(STREAM, "\tcmp %s, %s\n", ax, di);
        if(node -> kind == ND_EQ){
            fprintf(STREAM, "\tsete al\n");
        }
        else if(node -> kind == ND_NE){
            fprintf(STREAM, "\tsetne al\n");
        }
        else if(node -> kind == ND_LT){
            if(node -> lhs -> ty -> is_unsigned)
                fprintf(STREAM, "\tsetb al\n");
            else
                fprintf(STREAM, "\tsetl al\n");
        }
        else if(node -> kind == ND_LE){
            if(node -> lhs -> ty -> is_unsigned)
                fprintf(STREAM, "\tsetbe al\n");
            else 
                fprintf(STREAM, "\tsetle al\n");
        }
        fprintf(STREAM, "\tmovzx rax, al\n");
        return;

        error("invalid expression");
    }    
}

static void gen_stmt(Node* node){
    switch(node -> kind){
    
        case ND_RET:
            if(node -> lhs)
                gen_expr(node -> lhs);
            fprintf(STREAM, "\tjmp .L.end.%s\n", current_fn -> name);
            return;

        case ND_IF:{
            int idx = get_index();
            gen_expr(node -> cond);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tje .L.else.%d\n", idx); // 条件式が偽の時はelseに指定されているコードに飛ぶ
            gen_stmt(node -> then); // 条件式が真の時に実行される。
            fprintf(STREAM, "\tjmp .L.end.%d\n", idx);
            fprintf(STREAM, ".L.else.%d:\n", idx);
            if(node -> els){
                gen_stmt(node -> els); // 条件式が偽の時に実行される。
            }
            fprintf(STREAM, ".L.end.%d:\n", idx);
            return;
        }

        case ND_FOR:{
            int idx = get_index();
            if(node -> init){
                gen_stmt(node -> init);
            }
            fprintf(STREAM, ".L.begin.%d:\n", idx);
            if(node -> cond){
                gen_expr(node -> cond);
                fprintf(STREAM, "\tcmp rax, 0\n");
                fprintf(STREAM, "\tje %s\n", node -> brk_label); // 条件式が偽の時は終了

            }
            gen_stmt(node -> then); // thenは必ずあることが期待されている。
            fprintf(STREAM, "%s:\n", node -> cont_label);
            if(node -> inc){
                gen_expr(node -> inc);
            }
            fprintf(STREAM, "\tjmp .L.begin.%d\n", idx); // 条件式の評価に戻る
            fprintf(STREAM, "%s:\n", node -> brk_label);
            return;
        }
        
        case ND_DO:{
            int idx = get_index();
            fprintf(STREAM, ".L.begin.%d:\n", idx);
            gen_stmt(node -> then);
            fprintf(STREAM, "%s:\n", node -> cont_label);
            gen_expr(node -> cond);
            fprintf(STREAM, "\tcmp rax, 0\n");
            fprintf(STREAM, "\tjne .L.begin.%d\n", idx);
            fprintf(STREAM, "%s:\n", node -> brk_label);
            return;
        }

        case ND_GOTO:
            fprintf(STREAM, "\tjmp %s\n", node -> unique_label);
            return;
        
        case ND_LABEL:
            fprintf(STREAM, "%s:\n", node -> unique_label);
            gen_stmt(node -> lhs);
            return;
        
        case ND_SWITCH:
            gen_expr(node -> cond);
            for(Node *n = node -> case_next; n; n = n -> case_next){
                char *reg = (node -> cond -> ty -> size == 8) ? "rax" : "eax";
                fprintf(STREAM, "\tcmp %s, %ld\n", reg, n -> val);
                fprintf(STREAM, "\tje %s\n", n -> unique_label);
            }
            if(node -> default_case)
                fprintf(STREAM, "jmp %s\n", node -> default_case -> unique_label);
            // 該当するcaseがなかった時
            fprintf(STREAM, "\tjmp %s\n", node -> brk_label);

            gen_stmt(node -> then);
            fprintf(STREAM, "%s:\n", node -> brk_label);
            return;
        
        case ND_CASE:
            fprintf(STREAM, "%s:", node -> unique_label);
            gen_stmt(node -> lhs);
            return;
        
        case ND_BLOCK:
            for(Node *stmt = node -> body; stmt; stmt = stmt -> next){
                gen_stmt(stmt);
            }
            return;

        case ND_EXPR_STMT:
            gen_expr(node -> lhs);
            return;   
    }
}

static void store_arg(int i, int offset, unsigned int size){
    switch(size){
        case 1:
            fprintf(STREAM, "\tmov [rbp + %d], %s\n", offset, argreg8[i]);
            return;
        
        case 2:
            fprintf(STREAM, "\tmov [rbp + %d], %s\n", offset, argreg16[i]);
            return;
        
        case 4:
            fprintf(STREAM, "\tmov [rbp + %d], %s\n", offset, argreg32[i]);
            return;
        
        default:
            fprintf(STREAM, "\tmov [rbp + %d], %s\n", offset, argreg64[i]);
            return;
    }
}

int align_to(int offset, int align){
    return (offset + align - 1) / align * align;
}

static void assign_lvar_offsets(Obj *globals){
    for(Obj *fn = globals; fn; fn = fn -> next){
        if(!is_func(fn -> ty)){
            continue;
        }

        int offset = 0;
        for(Obj *lvar = fn -> locals; lvar; lvar = lvar ->next){
            offset += lvar -> ty -> size;
            offset = align_to(offset, lvar -> align);
            lvar -> offset = -offset;
        }
        fn -> stack_size = align_to(offset, 16);
    }
}

static void emit_data(Obj *globals){
    for(Obj *gvar = globals; gvar; gvar = gvar -> next){
        if(is_func(gvar -> ty) || !gvar -> is_definition){
            continue;
        }

        if(gvar -> is_static)
            fprintf(STREAM, ".local %s\n", gvar -> name);
        else 
            fprintf(STREAM, ".global %s\n", gvar -> name); 
        fprintf(STREAM, ".align %d\n", gvar -> align);
        
        if(gvar -> init_data){
            fprintf(STREAM, ".data\n");
            fprintf(STREAM, "%s:\n", gvar -> name);
            int pos = 0;
            Relocation *rel = gvar -> rel;
            while(pos < gvar -> ty -> size){
                // offset == posは、.byteを使っているために必要なチェック。
                if(rel && rel -> offset == pos){
                    fprintf(STREAM, "\t.quad %s + %ld\n", rel -> label, rel -> addend);
                    rel = rel -> next;
                    pos += 8;
                }else{
                    fprintf(STREAM, "\t.byte %d\n", gvar -> init_data[pos++]);
                }
            }
        }else{
            fprintf(STREAM, ".bss\n");
            fprintf(STREAM, "%s:\n", gvar -> name);
            fprintf(STREAM, "\t.zero %d\n", gvar -> ty -> size);
        }
    }
}

static void emit_text(Obj *globals){
    fprintf(STREAM, ".text\n");
    for(Obj *fn = globals; fn; fn = fn -> next){
        if(!is_func(fn -> ty) || !fn -> is_definition){
            continue;
        }
        current_fn = fn;
       
        if(fn -> is_static)
            fprintf(STREAM, ".local %s\n", fn -> name);
        else
            fprintf(STREAM, ".global %s\n", fn -> name);
        
        fprintf(STREAM, "%s:\n", fn -> name);

        /* プロローグ。 */
        fprintf(STREAM, "\tpush rbp\n");
        fprintf(STREAM, "\tmov rbp, rsp\n");
        fprintf(STREAM, "\tsub rsp, %u\n", fn -> stack_size);

        // 可変長引数関数
        if(fn -> va_area){
            int gp = 0;
            for(Obj *var = fn -> params; var; var = var ->next)
                gp++;
            int off = fn -> va_area -> offset;

            // va_elem
            fprintf(STREAM, "\tmov [rbp + %d], DWORD PTR %d\n", off, gp * 8);
            fprintf(STREAM, "\tmov [rbp + %d], DWORD PTR 0\n", off + 4);
            fprintf(STREAM, "\tmovq [rbp + %d], rbp\n", off + 16);
            fprintf(STREAM, "\taddq [rbp + %d], %d\n", off + 16, off + 24);
            // __reg_save_area__
            fprintf(STREAM, "\tmovq [rbp + %d], rdi\n", off + 24);
            fprintf(STREAM, "\tmovq [rbp + %d], rsi\n", off + 32);
            fprintf(STREAM, "\tmovq [rbp + %d], rdx\n", off + 40);
            fprintf(STREAM, "\tmovq [rbp + %d], rcx\n", off + 48);
            fprintf(STREAM, "\tmovq [rbp + %d], r8\n", off + 56);
            fprintf(STREAM, "\tmovq [rbp + %d], r9\n", off + 64);
            fprintf(STREAM, "\tmovsd [rbp + %d], xmm0\n", off + 72);
            fprintf(STREAM, "\tmovsd [rbp + %d], xmm1\n", off + 80);
            fprintf(STREAM, "\tmovsd [rbp + %d], xmm2\n", off + 88);
            fprintf(STREAM, "\tmovsd [rbp + %d], xmm3\n", off + 96);
            fprintf(STREAM, "\tmovsd [rbp + %d], xmm4\n", off + 104);
            fprintf(STREAM, "\tmovsd [rbp + %d], xmm5\n", off + 112);
            fprintf(STREAM, "\tmovsd [rbp + %d], xmm6\n", off + 120);
            fprintf(STREAM, "\tmovsd [rbp + %d], xmm7\n", off + 128);
        }

        int i = 0;
        /* パラメータをスタック領域にコピー */
        for(Obj *var = fn -> params; var; var = var -> next)
            store_arg(i++, var -> offset, var -> ty -> size);
    
        /* コード生成 */
        gen_stmt(fn -> body);
        assert(depth == 0); //プロローグで確保したスタックフレーム以外の領域を使っていないことをチェック

        /* エピローグ */
        fprintf(STREAM, ".L.end.%s:\n", fn -> name); // このラベルは関数ごと。
        fprintf(STREAM, "\tmov rsp, rbp\n");
        fprintf(STREAM, "\tpop rbp\n");
        fprintf(STREAM, "\tret\n"); /* 最後の式の評価結果が返り値になる。*/   
    }
}

void codegen(Obj *globals){
    fprintf(STREAM, ".intel_syntax noprefix\n");
    assign_lvar_offsets(globals);
    emit_data(globals);
    emit_text(globals);
}
