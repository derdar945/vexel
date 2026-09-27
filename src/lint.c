#include "lint.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- used-cell tracking (flat, small) ---- */

typedef struct Use {
    char name[64];
    int def_line;
    int used;
    struct Use *next;
} Use;

typedef struct LCtx {
    const char *path;
    int warns;
    Use *cells;   /* top-level cells */
    int in_tool;  /* tool bodies get their own scope; skip for v1 */
} LCtx;

static void warn(LCtx *c, int line, int col, const char *msg) {
    printf("%s:%d:%d: warning: %s\n", c->path, line, col, msg);
    c->warns++;
}

static Use *find_cell(LCtx *c, const char *name) {
    for (Use *u = c->cells; u; u = u->next) {
        if (strcmp(u->name, name) == 0) return u;
    }
    return NULL;
}

static void def_cell(LCtx *c, const char *name, int line) {
    if (c->in_tool) return;
    if (find_cell(c, name)) return; /* reforge is legal */
    Use *u = (Use *)calloc(1, sizeof(Use));
    if (!u) return;
    snprintf(u->name, sizeof(u->name), "%s", name);
    u->def_line = line;
    u->next = c->cells;
    c->cells = u;
}

static void use_cell(LCtx *c, const char *name) {
    if (c->in_tool) return;
    Use *u = find_cell(c, name);
    if (u) u->used = 1;
}

/* ---- expression walk ---- */

static int expr_is_lit(const VxExpr *e, double *num, int *is_text_empty) {
    if (!e) return 0;
    if (e->kind == E_NUM) {
        if (num) *num = e->u.num;
        return 1;
    }
    if (e->kind == E_TEXT) {
        if (is_text_empty) *is_text_empty = (e->u.text[0] == '\0');
        return 2;
    }
    if (e->kind == E_FAIL) return 3;
    return 0;
}

static void walk_expr(LCtx *c, const VxExpr *e);

static void walk_call(LCtx *c, const VxExpr *e) {
    for (int i = 0; i < e->u.call.nargs; i++) walk_expr(c, e->u.call.args[i]);
}

static void walk_expr(LCtx *c, const VxExpr *e) {
    if (!e) return;
    switch (e->kind) {
        case E_NUM:
        case E_TEXT:
        case E_FAIL:
        case E_IT:
            break;
        case E_VAR:
            use_cell(c, e->u.varname);
            break;
        case E_UNARY:
            walk_expr(c, e->u.unary.rhs);
            break;
        case E_BINARY: {
            /* constant divide/mod by zero */
            if ((e->u.binary.op == B_DIV || e->u.binary.op == B_MOD)) {
                double n = 0;
                if (expr_is_lit(e->u.binary.rhs, &n, NULL) == 1 && n == 0.0)
                    warn(c, e->line, e->col, "division by zero is fail");
            }
            walk_expr(c, e->u.binary.lhs);
            walk_expr(c, e->u.binary.rhs);
            break;
        }
        case E_CALL:
            walk_call(c, e);
            break;
        case E_RESCUE:
            walk_expr(c, e->u.rescue.lhs);
            walk_expr(c, e->u.rescue.rhs);
            break;
        case E_VEC:
            for (int i = 0; i < e->u.vec.nitems; i++) walk_expr(c, e->u.vec.items[i]);
            break;
    }
}

static void walk_beats(LCtx *c, VxBeat *b);

static void walk_tool_def(LCtx *c, VxBeat *b) {
    /* shadowing a built-in tool name */
    const char *n = b->u.tool.name;
    if (strcmp(n, "len") == 0 || strcmp(n, "at") == 0 ||
        strcmp(n, "type") == 0 || strcmp(n, "str") == 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "tool '%s' shadows a built-in", n);
        warn(c, b->line, b->col, msg);
    }
    int save = c->in_tool;
    c->in_tool = 1;
    walk_beats(c, b->u.tool.body);
    c->in_tool = save;
}

static void walk_beats(LCtx *c, VxBeat *b) {
    for (; b; b = b->next) {
        switch (b->kind) {
            case B_CELL:
                walk_expr(c, b->u.cell.expr);
                def_cell(c, b->u.cell.name, b->line);
                break;
            case B_SHOW:
                walk_expr(c, b->u.show.expr);
                break;
            case B_EXPR:
                walk_expr(c, b->u.expr.expr);
                break;
            case B_GIVE:
                walk_expr(c, b->u.give.expr);
                break;
            case B_ASK: {
                double n = 0;
                int k = expr_is_lit(b->u.ask.cond, &n, NULL);
                if (k == 1)
                    warn(c, b->line, b->col,
                         n != 0.0 ? "ask is always wet" : "ask is always dry");
                else if (k == 3)
                    warn(c, b->line, b->col, "ask on fail is always dry");
                walk_expr(c, b->u.ask.cond);
                if (!b->u.ask.thenb && !b->u.ask.elseb)
                    warn(c, b->line, b->col, "ask with no branches");
                walk_beats(c, b->u.ask.thenb);
                walk_beats(c, b->u.ask.elseb);
                break;
            }
            case B_LOOP: {
                double n = 0;
                if (expr_is_lit(b->u.loop.count, &n, NULL) == 1 && (long)n <= 0)
                    warn(c, b->line, b->col, "circle never turns");
                walk_expr(c, b->u.loop.count);
                walk_beats(c, b->u.loop.body);
                break;
            }
            case B_TOOL:
                walk_tool_def(c, b);
                break;
            case B_ATTACH:
                walk_expr(c, NULL);
                break;
        }
    }
}

int vx_lint(const char *src, const char *path) {
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
    LCtx c;
    memset(&c, 0, sizeof(c));
    c.path = path;
    walk_beats(&c, ast.head);
    for (Use *u = c.cells; u; u = u->next) {
        if (!u->used) {
            printf("%s:%d:1: warning: cell '@ %s' never read\n", path, u->def_line, u->name);
            c.warns++;
        }
    }
    while (c.cells) {
        Use *nx = c.cells->next;
        free(c.cells);
        c.cells = nx;
    }
    vx_ast_free(&ast);
    if (c.warns) {
        printf("%s: %d warning(s)\n", path, c.warns);
        return 1;
    }
    printf("%s: clean\n", path);
    return 0;
}
