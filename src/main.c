#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vexel.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "opman.h"
#include "fmt.h"
#include "lint.h"
#include "vtest.h"
#include "lsp.h"
#include "dbg.h"

static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) n = 0;
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = n ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    buf[r] = '\0';
    if (len_out) *len_out = r;
    return buf;
}

static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (b > a) return 0;
    return strcmp(s + a - b, suf) == 0;
}

static int compile_source(const char *src, VxProgram *prog, VxError *err) {
    VxTokVec toks;
    if (!vx_lex(src, &toks, err)) return 0;
    VxAst ast;
    memset(&ast, 0, sizeof(ast));
    if (!vx_parse(&toks, &ast, err)) { vx_tokvec_free(&toks); return 0; }
    vx_tokvec_free(&toks);
    if (!vx_compile(&ast, prog, err)) { vx_ast_free(&ast); return 0; }
    vx_ast_free(&ast);
    return 1;
}

static void print_err(const char *what, VxError *e) {
    if (e->line > 0)
        fprintf(stderr, "%s: %d:%d: %s\n", what, e->line, e->col, e->msg);
    else
        fprintf(stderr, "%s: %s\n", what, e->msg);
}

static int cmd_run(const char *path) {
    VxProgram prog;
    VxError err;
    memset(&err, 0, sizeof(err));
    if (ends_with(path, ".vxb")) {
        if (!vx_load(path, &prog, &err)) { print_err("vexel", &err); return 1; }
    } else {
        size_t n = 0;
        char *src = read_file(path, &n);
        if (!src) { fprintf(stderr, "vexel: cannot read %s\n", path); return 1; }
        int ok = compile_source(src, &prog, &err);
        free(src);
        if (!ok) { print_err("vexel", &err); return 1; }
    }
    VxVM vm;
    vx_vm_init(&vm, &prog);
    int rc = vx_vm_run(&vm, &err);
    if (rc != 0) print_err("vexel", &err);
    vx_vm_free(&vm);
    vx_program_free(&prog);
    return rc;
}

static int cmd_check(const char *path, int verbose) {
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) { fprintf(stderr, "vexel: cannot read %s\n", path); return 1; }
    VxProgram prog;
    VxError err;
    memset(&err, 0, sizeof(err));
    int ok = compile_source(src, &prog, &err);
    free(src);
    if (!ok) { print_err("vexel", &err); return 1; }
    printf("ok %s\n", path);
    if (verbose) vx_disasm(&prog);
    vx_program_free(&prog);
    return 0;
}

static int cmd_build(const char *path, const char *out) {
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) { fprintf(stderr, "vexel: cannot read %s\n", path); return 1; }
    VxProgram prog;
    VxError err;
    memset(&err, 0, sizeof(err));
    int ok = compile_source(src, &prog, &err);
    free(src);
    if (!ok) { print_err("vexel", &err); return 1; }
    char def[1024];
    const char *o = out;
    if (!o) {
        size_t L = strlen(path);
        if (L > 3 && strcmp(path + L - 3, ".vx") == 0) {
            snprintf(def, sizeof(def), "%.*s.vxb", (int)(L - 3), path);
        } else {
            snprintf(def, sizeof(def), "%s.vxb", path);
        }
        o = def;
    }
    if (!vx_save(&prog, o, &err)) { print_err("vexel", &err); vx_program_free(&prog); return 1; }
    printf("wrote %s\n", o);
    vx_program_free(&prog);
    return 0;
}

static int cmd_fmt(const char *path, int write_back) {
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) { fprintf(stderr, "vexel: cannot read %s\n", path); return 1; }
    char *out = vx_format(src);
    free(src);
    if (!out) { fprintf(stderr, "vexel: out of memory\n"); return 1; }
    if (write_back) {
        FILE *f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "vexel: cannot write %s\n", path); free(out); return 1; }
        fwrite(out, 1, strlen(out), f);
        fclose(f);
        printf("formatted %s\n", path);
    } else {
        fwrite(out, 1, strlen(out), stdout);
    }
    free(out);
    return 0;
}

static int cmd_lint(const char *path) {
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) { fprintf(stderr, "vexel: cannot read %s\n", path); return 2; }
    int rc = vx_lint(src, path);
    free(src);
    return rc;
}

static int cmd_test(const char *path) {
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) { fprintf(stderr, "vexel: cannot read %s\n", path); return 2; }
    int rc = vx_test_file(src, path);
    free(src);
    return rc;
}

static int cmd_debug(const char *path) {
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) { fprintf(stderr, "vexel: cannot read %s\n", path); return 2; }
    int rc = vx_debug_file(src, path);
    free(src);
    return rc;
}

/* ---------- repl ---------- */

typedef struct Out {
    char **lines;
    int n;
    int cap;
} Out;

static void repl_show(const char *line, void *ud) {
    Out *o = (Out *)ud;
    if (o->n + 1 > o->cap) {
        int nc = o->cap ? o->cap * 2 : 16;
        char **nd = (char **)realloc(o->lines, (size_t)nc * sizeof(char *));
        if (!nd) return;
        o->lines = nd;
        o->cap = nc;
    }
    o->lines[o->n++] = vx_strdup(line ? line : "fail");
}

static void out_free(Out *o) {
    for (int i = 0; i < o->n; i++) free(o->lines[i]);
    free(o->lines);
    memset(o, 0, sizeof(*o));
}

static void sess_append(char **sess, size_t *len, size_t *cap, const char *s) {
    size_t L = strlen(s);
    if (*len + L + 2 > *cap) {
        size_t nc = (*cap ? *cap * 2 : 1024);
        while (nc < *len + L + 2) nc *= 2;
        char *nd = (char *)realloc(*sess, nc);
        if (!nd) return;
        *sess = nd;
        *cap = nc;
    }
    memcpy(*sess + *len, s, L);
    *len += L;
    (*sess)[*len] = '\0';
}

/* depth of unclosed ? * & blocks by first-rune of each line */
static int pending_depth(const char *s) {
    int d = 0;
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '?' || *p == '*' || *p == '&') d++;
        else if (*p == '.') d--;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return d;
}

static int is_word(const char *s, const char *w) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t L = strlen(w);
    if (strncmp(s, w, L) != 0) return 0;
    char c = s[L];
    return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int cmd_repl(void) {
    printf("Vexel %s\n", VEXEL_VERSION);
    printf("; marks: @ cell   & tool   > show   ? ! . ask   * . circle   = give   # summon   ?? rescue\n");
    printf("; type 'bye' to leave\n");
    char *sess = NULL;
    size_t slen = 0, scap = 0;
    sess_append(&sess, &slen, &scap, "");
    int shown = 0;
    Out acc;
    memset(&acc, 0, sizeof(acc));
    char *pending = NULL;
    size_t plen = 0, pcap = 0;
    sess_append(&pending, &plen, &pcap, "");
    char line[4096];
    for (;;) {
        int d = pending_depth(pending);
        printf("%s ", d > 0 ? ".." : "vx>");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        if (d <= 0 && (is_word(line, "bye") || is_word(line, ".exit") || is_word(line, ".quit"))) break;
        sess_append(&pending, &plen, &pcap, line);
        if (pending_depth(pending) > 0) continue;
        /* complete fragment */
        size_t oldlen = slen;
        sess_append(&sess, &slen, &scap, pending);
        VxProgram prog;
        VxError err;
        memset(&err, 0, sizeof(err));
        if (!compile_source(sess, &prog, &err)) {
            print_err("vexel", &err);
            sess[oldlen] = '\0';
            slen = oldlen;
        } else {
            Out now;
            memset(&now, 0, sizeof(now));
            VxVM vm;
            vx_vm_init(&vm, &prog);
            vm.show_cb = repl_show;
            vm.show_ud = &now;
            int rc = vx_vm_run(&vm, &err);
            if (rc != 0) {
                print_err("vexel", &err);
                sess[oldlen] = '\0';
                slen = oldlen;
            } else {
                /* session replays from scratch; prefix output is identical,
                   so only lines past `shown` are fresh */
                for (int i = shown; i < now.n; i++) printf("%s\n", now.lines[i]);
                out_free(&acc);
                acc = now;
                shown = acc.n;
                memset(&now, 0, sizeof(now));
            }
            vx_vm_free(&vm);
            vx_program_free(&prog);
        }
        pending[0] = '\0';
        plen = 0;
    }
    free(sess);
    free(pending);
    out_free(&acc);
    printf("bye\n");
    return 0;
}

static void usage(void) {
    printf("\n  VEXEL %s — marks bench\n\n", VEXEL_VERSION);
    printf("  !vex_run <file.vx|file.vxb>\n");
    printf("  !vex_check <file.vx> [-v]\n");
    printf("  !vex_build <file.vx> [out.vxb]\n");
    printf("  !vex_fmt <file.vx> [-w]\n");
    printf("  !vex_lint <file.vx>\n");
    printf("  !vex_test <file.vx>\n");
    printf("  !vex_debug <file.vx>\n");
    printf("  !vex_lsp (stdio language server)\n");
    printf("  !vex_repl\n");
    printf("  !vex_version\n");
    printf("  !vex_help\n");
    printf("  !vex_new <Project>\n");
    printf("  !vex_add <Operator>\n");
    printf("  !vex_remove <Operator>\n");
    printf("  !vex_update <Operator>\n");
    printf("  !vex_list\n");
    printf("  !vex_info <Operator>\n");
    printf("  !vex_search <term>\n");
    printf("  !vex_operator_new <Operator>\n");
    printf("  !vex_operator_build [dir]\n");
    printf("  !vex_operator_install <dir>\n");
    printf("  !vex_registry_add <dir|http>\n");
    printf("  !vex_registry_list\n");
    printf("  !vex_registry_remove <dir|http>\n\n");
}

static void help(void) {
    usage();
    printf("  Language marks: @ cell   & tool   > show   ? ! . ask\n");
    printf("                  * . circle   = give   # summon   ?? rescue\n");
    printf("  Place an operator:  @ VexGUI\n");
    printf("  Docs: docs/ (language, operators, vexgui, cli)\n\n");
}

static int need_arg(int argc, char **argv, int i, const char *cmd,
                    const char *what) {
    if (i >= argc) {
        fprintf(stderr, "vexel: %s wants %s\n", cmd, what);
        return 0;
    }
    (void)argv;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    const char *c = argv[1];
    if (strcmp(c, "!vex_version") == 0) {
        printf("Vexel %s\n", VEXEL_VERSION);
        return 0;
    }
    if (strcmp(c, "!vex_help") == 0) { help(); return 0; }
    if (strcmp(c, "!vex_run") == 0) {
        if (!need_arg(argc, argv, 2, c, "a file")) return 1;
        return cmd_run(argv[2]);
    }
    if (strcmp(c, "!vex_check") == 0) {
        if (!need_arg(argc, argv, 2, c, "a file")) return 1;
        int vb = (argc >= 4 && strcmp(argv[3], "-v") == 0);
        return cmd_check(argv[2], vb);
    }
    if (strcmp(c, "!vex_build") == 0) {
        if (!need_arg(argc, argv, 2, c, "a file")) return 1;
        return cmd_build(argv[2], argc >= 4 ? argv[3] : NULL);
    }
    if (strcmp(c, "!vex_fmt") == 0) {
        if (!need_arg(argc, argv, 2, c, "a file")) return 1;
        int w = (argc >= 4 && strcmp(argv[3], "-w") == 0);
        return cmd_fmt(argv[2], w);
    }
    if (strcmp(c, "!vex_lint") == 0) {
        if (!need_arg(argc, argv, 2, c, "a file")) return 1;
        return cmd_lint(argv[2]);
    }
    if (strcmp(c, "!vex_lsp") == 0) return vx_lsp_run();
    if (strcmp(c, "!vex_test") == 0) {
        if (!need_arg(argc, argv, 2, c, "a file")) return 1;
        return cmd_test(argv[2]);
    }
    if (strcmp(c, "!vex_debug") == 0) {
        if (!need_arg(argc, argv, 2, c, "a file")) return 1;
        return cmd_debug(argv[2]);
    }
    if (strcmp(c, "!vex_repl") == 0) return cmd_repl();
    if (strcmp(c, "!vex_new") == 0) {
        if (!need_arg(argc, argv, 2, c, "a project name")) return 1;
        return vx_opm_new_project(argv[2]);
    }
    if (strcmp(c, "!vex_add") == 0) {
        if (!need_arg(argc, argv, 2, c, "an operator name")) return 1;
        return vx_opm_add(argv[2]);
    }
    if (strcmp(c, "!vex_remove") == 0) {
        if (!need_arg(argc, argv, 2, c, "an operator name")) return 1;
        return vx_opm_remove(argv[2]);
    }
    if (strcmp(c, "!vex_update") == 0) {
        if (!need_arg(argc, argv, 2, c, "an operator name")) return 1;
        return vx_opm_update(argv[2]);
    }
    if (strcmp(c, "!vex_list") == 0) return vx_opm_list();
    if (strcmp(c, "!vex_info") == 0) {
        if (!need_arg(argc, argv, 2, c, "an operator name")) return 1;
        return vx_opm_info(argv[2]);
    }
    if (strcmp(c, "!vex_search") == 0) {
        if (!need_arg(argc, argv, 2, c, "a term")) return 1;
        return vx_opm_search(argv[2]);
    }
    if (strcmp(c, "!vex_operator_new") == 0) {
        if (!need_arg(argc, argv, 2, c, "an operator name")) return 1;
        return vx_opm_new_operator(argv[2]);
    }
    if (strcmp(c, "!vex_operator_build") == 0) {
        return vx_opm_build_operator(argc >= 3 ? argv[2] : ".");
    }
    if (strcmp(c, "!vex_operator_install") == 0) {
        if (!need_arg(argc, argv, 2, c, "a directory")) return 1;
        return vx_opm_install_path(argv[2]);
    }
    if (strcmp(c, "!vex_registry_add") == 0) {
        if (!need_arg(argc, argv, 2, c, "a dir or http base")) return 1;
        return vx_opm_registry_add(argv[2]);
    }
    if (strcmp(c, "!vex_registry_list") == 0) return vx_opm_registry_list();
    if (strcmp(c, "!vex_registry_remove") == 0) {
        if (!need_arg(argc, argv, 2, c, "a dir or http base")) return 1;
        return vx_opm_registry_remove(argv[2]);
    }
    fprintf(stderr, "vexel: strange word '%s'\n", c);
    usage();
    return 1;
}
