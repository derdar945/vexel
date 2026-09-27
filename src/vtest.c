#include "vtest.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int vx_test_file(const char *src, const char *path) {
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

    /* setup: run the bench once */
    VxVM vm;
    vx_vm_init(&vm, &prog);
    if (vx_vm_run(&vm, &err)) {
        printf("%s: setup failed: %s\n", path, err.msg);
        vx_vm_free(&vm);
        vx_program_free(&prog);
        return 2;
    }

    int pass = 0, fail = 0, skip = 0;
    for (int i = 0; i < prog.ntools; i++) {
        const char *nm = prog.tools[i].name;
        if (strncmp(nm, "test_", 5) != 0) continue;
        if (prog.tools[i].nparams != 0) {
            printf("SKIP %s (wants marks)\n", nm);
            skip++;
            continue;
        }
        VxOpVal *out = NULL;
        memset(&err, 0, sizeof(err));
        int rc = vx_vm_summon(&vm, nm, NULL, 0, &out, &err);
        if (rc != 0 || !out) {
            printf("FAIL %s (%s)\n", nm, err.has ? err.msg : "raised fail");
            if (out) {
                vx_release((VxValue *)out);
                free(out);
            }
            fail++;
            continue;
        }
        int wet = vx_is_true(*(VxValue *)out);
        vx_release((VxValue *)out);
        free(out);
        if (wet) {
            printf("PASS %s\n", nm);
            pass++;
        } else {
            printf("FAIL %s (dry answer)\n", nm);
            fail++;
        }
    }
    vx_vm_free(&vm);
    vx_program_free(&prog);
    printf("%s: %d passed, %d failed, %d skipped\n", path, pass, fail, skip);
    return fail ? 1 : 0;
}
