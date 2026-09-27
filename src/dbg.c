#include "dbg.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void show_pos(VxVM *vm) {
    if (!vx_vm_live(vm)) {
        printf("-- landed.\n");
        return;
    }
    VxFrame *f = &vm->frames[vm->nframes - 1];
    const char *tname = f->tool < 0 ? "main" : f->w->name;
    if (f->ip < 0 || f->ip >= f->w->ncode) {
        printf("-- %s:%d past the bench\n", tname, f->ip);
        return;
    }
    VxInstr in = f->w->code[f->ip];
    printf("-- %s:%04d %-10s %d %d   [stack %d, frames %d]\n", tname, f->ip,
           vx_op_name(in.op), in.a, in.b, vm->sp, vm->nframes);
}

static void show_globals(VxVM *vm) {
    VxProgram *p = vm->prog;
    if (!p->nglobals) {
        printf("(no cells)\n");
        return;
    }
    for (int i = 0; i < p->nglobals; i++) {
        char *r = vx_repr(vm->globals[i]);
        printf("  @ %s : %s\n", p->globals[i], r ? r : "fail");
        free(r);
    }
}

static void show_stack(VxVM *vm) {
    if (!vm->sp) {
        printf("(bench empty)\n");
        return;
    }
    int from = vm->sp > 8 ? vm->sp - 8 : 0;
    if (from) printf("  ... (%d deeper)\n", from);
    for (int i = from; i < vm->sp; i++) {
        char *r = vx_repr(vm->stack[i]);
        printf("  [%d] %s\n", i, r ? r : "fail");
        free(r);
    }
}

static void show_dis(VxVM *vm) {
    if (!vx_vm_live(vm)) {
        printf("(landed)\n");
        return;
    }
    VxFrame *f = &vm->frames[vm->nframes - 1];
    int lo = f->ip - 3 < 0 ? 0 : f->ip - 3;
    int hi = f->ip + 4 > f->w->ncode ? f->w->ncode : f->ip + 4;
    for (int i = lo; i < hi; i++) {
        VxInstr in = f->w->code[i];
        printf("  %c %04d %-10s %d %d\n", i == f->ip ? '>' : ' ', i,
               vx_op_name(in.op), in.a, in.b);
    }
}

int vx_debug_file(const char *src, const char *path) {
    VxTokVec toks;
    VxError err;
    memset(&err, 0, sizeof(err));
    if (!vx_lex(src, &toks, &err)) {
        printf("%s:%d:%d: error: %s\n", path, err.line, err.col, err.msg);
        return 2;
    }
    VxAst ast;
    memset(&ast, 0, sizeof(ast));
    if (!vx_parse(&toks, &ast, &err)) {
        printf("%s:%d:%d: error: %s\n", path, err.line, err.col, err.msg);
        vx_tokvec_free(&toks);
        return 2;
    }
    vx_tokvec_free(&toks);
    VxProgram prog;
    if (!vx_compile(&ast, &prog, &err)) {
        printf("%s:%d:%d: error: %s\n", path, err.line, err.col, err.msg);
        vx_ast_free(&ast);
        return 2;
    }
    vx_ast_free(&ast);

    VxVM vm;
    vx_vm_init(&vm, &prog);
    if (vx_vm_start(&vm, &err)) {
        printf("vexel: %s\n", err.msg);
        vx_vm_free(&vm);
        vx_program_free(&prog);
        return 1;
    }
    printf("Vexel dbg — %s\n", path);
    printf("s[tep] [N]  r[un]  g[lobals]  v[stack]  d[isasm]  q[uit]\n");
    show_pos(&vm);
    char line[256];
    int rc = 0;
    for (;;) {
        printf("vdb> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        /* verb + optional number */
        char verb = 0;
        long num = 1;
        if (sscanf(line, " %c%ld", &verb, &num) < 1) continue;
        if (num < 1) num = 1;
        if (num > 1000000) num = 1000000;
        if (verb == 'q') break;
        if (verb == 'g') {
            show_globals(&vm);
            continue;
        }
        if (verb == 'v') {
            show_stack(&vm);
            continue;
        }
        if (verb == 'd') {
            show_dis(&vm);
            continue;
        }
        if (verb == 'h' || verb == '?') {
            printf("s[tep] [N]  r[un]  g[lobals]  v[stack]  d[isasm]  q[uit]\n");
            continue;
        }
        if (verb == 'r') num = 1000000000L;
        if (verb != 's' && verb != 'r') {
            printf("strange mark '%c' (try h)\n", verb);
            continue;
        }
        for (long i = 0; i < num; i++) {
            if (!vx_vm_live(&vm)) break;
            memset(&err, 0, sizeof(err));
            int st = vx_vm_step(&vm, &err);
            if (st < 0) {
                printf("vexel: %s\n", err.has ? err.msg : "bad mark");
                rc = 1;
                goto done;
            }
            if (st == 0) break;
        }
        show_pos(&vm);
        if (!vx_vm_live(&vm)) break;
    }
done:
    vx_vm_free(&vm);
    vx_program_free(&prog);
    return rc;
}
