#include "parser.h"
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    VxTokVec *t;
    int pos;
    VxError *err;
} Parser;

static VxToken *peek(Parser *p) { return &p->t->data[p->pos]; }
static VxToken *nexttok(Parser *p) {
    if (p->pos < p->t->len) p->pos++;
    return peek(p);
}
static int at(Parser *p, VxTokKind k) { return peek(p)->kind == k; }

static void skip_nl(Parser *p) {
    while (p->pos < p->t->len && p->t->data[p->pos].kind == T_NEWLINE) p->pos++;
}

static bool expect(Parser *p, VxTokKind k, const char *what) {
    if (at(p, k)) { nexttok(p); return true; }
    VxToken *t = peek(p);
    vx_error_set(p->err, t->line, t->col, "want %s, see %s", what, vx_tok_name(t->kind));
    return false;
}

/* after a beat header/expr: need end of line (or block edge) */
static bool expect_eol(Parser *p) {
    VxToken *t = peek(p);
    if (t->kind == T_NEWLINE) { skip_nl(p); return true; }
    if (t->kind == T_EOF || t->kind == T_DOT || t->kind == T_BANG) return true;
    vx_error_set(p->err, t->line, t->col, "want end of line, see %s", vx_tok_name(t->kind));
    return false;
}

static int can_start_expr(VxTokKind k) {
    return k == T_NUMBER || k == T_STRING || k == T_FAIL || k == T_IT ||
           k == T_IDENT || k == T_LPAREN || k == T_LBRACKET ||
           k == T_NOT || k == T_MINUS;
}

static VxExpr *new_expr(VxExprKind k, int line, int col) {
    VxExpr *e = (VxExpr *)calloc(1, sizeof(VxExpr));
    if (!e) return NULL;
    e->kind = k; e->line = line; e->col = col;
    return e;
}

static VxBeat *new_beat(VxBeatKind k, int line, int col) {
    VxBeat *b = (VxBeat *)calloc(1, sizeof(VxBeat));
    if (!b) return NULL;
    b->kind = k; b->line = line; b->col = col;
    return b;
}

void vx_expr_free(VxExpr *e) {
    if (!e) return;
    switch (e->kind) {
        case E_TEXT: free(e->u.text); break;
        case E_VAR: free(e->u.varname); break;
        case E_UNARY: vx_expr_free(e->u.unary.rhs); break;
        case E_BINARY:
            vx_expr_free(e->u.binary.lhs);
            vx_expr_free(e->u.binary.rhs);
            break;
        case E_CALL:
            free(e->u.call.op);
            free(e->u.call.name);
            for (int i = 0; i < e->u.call.nargs; i++) vx_expr_free(e->u.call.args[i]);
            free(e->u.call.args);
            break;
        case E_RESCUE:
            vx_expr_free(e->u.rescue.lhs);
            vx_expr_free(e->u.rescue.rhs);
            break;
        case E_VEC:
            for (int i = 0; i < e->u.vec.nitems; i++) vx_expr_free(e->u.vec.items[i]);
            free(e->u.vec.items);
            break;
        default: break;
    }
    free(e);
}

void vx_beats_free(VxBeat *b) {
    while (b) {
        VxBeat *nx = b->next;
        switch (b->kind) {
            case B_CELL: free(b->u.cell.name); vx_expr_free(b->u.cell.expr); break;
            case B_SHOW: vx_expr_free(b->u.show.expr); break;
            case B_ASK:
                vx_expr_free(b->u.ask.cond);
                vx_beats_free(b->u.ask.thenb);
                vx_beats_free(b->u.ask.elseb);
                break;
            case B_LOOP:
                vx_expr_free(b->u.loop.count);
                vx_beats_free(b->u.loop.body);
                break;
            case B_TOOL:
                free(b->u.tool.name);
                for (int i = 0; i < b->u.tool.nparams; i++) free(b->u.tool.params[i]);
                free(b->u.tool.params);
                vx_beats_free(b->u.tool.body);
                break;
            case B_GIVE: vx_expr_free(b->u.give.expr); break;
            case B_EXPR: vx_expr_free(b->u.expr.expr); break;
            case B_ATTACH: free(b->u.attach.name); break;
        }
        free(b);
        b = nx;
    }
}

void vx_ast_free(VxAst *a) {
    if (!a) return;
    vx_beats_free(a->head);
    a->head = NULL;
}

/* forward */
static VxExpr *parse_expr(Parser *p);
static VxBeat *parse_beat(Parser *p);
static VxExpr *parse_or(Parser *p);

static VxExpr *parse_primary(Parser *p) {
    VxToken *t = peek(p);
    if (t->kind == T_NUMBER) {
        VxExpr *e = new_expr(E_NUM, t->line, t->col);
        if (!e) return NULL;
        e->u.num = t->num;
        nexttok(p);
        return e;
    }
    if (t->kind == T_STRING) {
        VxExpr *e = new_expr(E_TEXT, t->line, t->col);
        if (!e) return NULL;
        e->u.text = vx_strdup(t->lexeme ? t->lexeme : "");
        nexttok(p);
        return e;
    }
    if (t->kind == T_FAIL) {
        VxExpr *e = new_expr(E_FAIL, t->line, t->col);
        nexttok(p);
        return e;
    }
    if (t->kind == T_IT) {
        VxExpr *e = new_expr(E_IT, t->line, t->col);
        nexttok(p);
        return e;
    }
    if (t->kind == T_IDENT) {
        VxExpr *e = new_expr(E_VAR, t->line, t->col);
        if (!e) return NULL;
        e->u.varname = vx_strdup(t->lexeme);
        nexttok(p);
        return e;
    }
    if (t->kind == T_LPAREN) {
        nexttok(p);
        skip_nl(p);
        VxExpr *e = parse_expr(p);
        if (!e) return NULL;
        skip_nl(p);
        if (!expect(p, T_RPAREN, ")")) { vx_expr_free(e); return NULL; }
        return e;
    }
    if (t->kind == T_LBRACKET) {
        int l = t->line, c = t->col;
        nexttok(p);
        VxExpr *e = new_expr(E_VEC, l, c);
        if (!e) return NULL;
        VxExpr **items = NULL;
        int n = 0, cap = 0;
        for (;;) {
            skip_nl(p);
            if (at(p, T_RBRACKET)) { nexttok(p); break; }
            if (at(p, T_EOF)) {
                vx_error_set(p->err, peek(p)->line, peek(p)->col, "vector is not closed with ]");
                for (int i = 0; i < n; i++) vx_expr_free(items[i]);
                free(items); free(e);
                return NULL;
            }
            VxExpr *it = parse_expr(p);
            if (!it) {
                for (int i = 0; i < n; i++) vx_expr_free(items[i]);
                free(items); free(e);
                return NULL;
            }
            if (n + 1 > cap) {
                int nc = cap ? cap * 2 : 4;
                VxExpr **ni = (VxExpr **)realloc(items, (size_t)nc * sizeof(VxExpr *));
                if (!ni) { vx_expr_free(it); for (int i = 0; i < n; i++) vx_expr_free(items[i]); free(items); free(e); return NULL; }
                items = ni; cap = nc;
            }
            items[n++] = it;
            skip_nl(p);
            if (at(p, T_COMMA)) { nexttok(p); skip_nl(p); }
            /* space-separated: loop continues while next can start expr */
        }
        e->u.vec.items = items;
        e->u.vec.nitems = n;
        return e;
    }
    vx_error_set(p->err, t->line, t->col, "want value, see %s", vx_tok_name(t->kind));
    return NULL;
}

static VxExpr *parse_summon(Parser *p) {
    VxExpr *prim = parse_primary(p);
    if (!prim) return NULL;
    /* Op.verb # ... — qualified summon into one named operator.
       Only when two names join with a dot AND # follows: otherwise
       leave the tokens alone (`.` also ends blocks). Checked before
       the plain-`#` test, so `VexSYS.tick #` never reads as stray. */
    char *op = NULL;
    if (prim->kind == E_VAR && p->pos + 2 < p->t->len &&
        p->t->data[p->pos].kind == T_DOT &&
        p->t->data[p->pos + 1].kind == T_IDENT &&
        p->t->data[p->pos + 2].kind == T_HASH) {
        op = prim->u.varname;
        prim->u.varname = NULL;
        int l0 = prim->line, c0 = prim->col;
        free(prim);
        prim = NULL;
        char *verb = vx_strdup(p->t->data[p->pos + 1].lexeme);
        if (!verb) {
            free(op);
            return NULL;
        }
        p->pos += 2;
        VxExpr *e = new_expr(E_CALL, l0, c0);
        if (!e) {
            free(op);
            free(verb);
            return NULL;
        }
        e->u.call.op = op;
        e->u.call.name = verb;
        e->u.call.args = NULL;
        e->u.call.nargs = 0;
        nexttok(p); /* # */
        if (can_start_expr(peek(p)->kind)) {
            VxExpr **args = NULL;
            int n = 0, cap = 0;
            for (;;) {
                VxExpr *a = parse_or(p);
                if (!a) {
                    for (int i = 0; i < n; i++) vx_expr_free(args[i]);
                    free(args);
                    vx_expr_free(e);
                    return NULL;
                }
                if (n + 1 > cap) {
                    int nc = cap ? cap * 2 : 4;
                    VxExpr **ni = (VxExpr **)realloc(args, (size_t)nc * sizeof(VxExpr *));
                    if (!ni) { vx_expr_free(a); for (int i = 0; i < n; i++) vx_expr_free(args[i]); free(args); vx_expr_free(e); return NULL; }
                    args = ni; cap = nc;
                }
                args[n++] = a;
                if (at(p, T_COMMA)) { nexttok(p); continue; }
                break;
            }
            e->u.call.args = args;
            e->u.call.nargs = n;
        }
        return e;
    }
    if (!at(p, T_HASH)) return prim;
    if (prim->kind != E_VAR) {
        vx_error_set(p->err, prim->line, prim->col, "summon needs a name before #");
        vx_expr_free(prim);
        return NULL;
    }
    int l = prim->line, c = prim->col;
    char *name = prim->u.varname;
    prim->u.varname = NULL;
    free(prim);
    nexttok(p); /* # */
    VxExpr *e = new_expr(E_CALL, l, c);
    if (!e) {
        free(name);
        return NULL;
    }
    e->u.call.op = NULL;
    e->u.call.name = name;
    e->u.call.args = NULL;
    e->u.call.nargs = 0;
    /* summon lives on one line: a newline right after # means zero marks.
       (Otherwise `#` would drink the newline and the next line's rune
       would parse as an operator — e.g. `>` as greater-than.) */
    if (can_start_expr(peek(p)->kind)) {
        VxExpr **args = NULL;
        int n = 0, cap = 0;
        for (;;) {
            /* args stop before ??: `f # a ?? b` is `(f # a) ?? b`.
               parenthesize to smuggle a rescue inside: `f # (a ?? b)` */
            VxExpr *a = parse_or(p);
            if (!a) {
                for (int i = 0; i < n; i++) vx_expr_free(args[i]);
                free(args); free(op); free(name); free(e);
                return NULL;
            }
            if (n + 1 > cap) {
                int nc = cap ? cap * 2 : 4;
                VxExpr **ni = (VxExpr **)realloc(args, (size_t)nc * sizeof(VxExpr *));
                if (!ni) { vx_expr_free(a); for (int i = 0; i < n; i++) vx_expr_free(args[i]); free(args); free(op); free(name); free(e); return NULL; }
                args = ni; cap = nc;
            }
            args[n++] = a;
            if (at(p, T_COMMA)) { nexttok(p); continue; }
            break;
        }
        e->u.call.args = args;
        e->u.call.nargs = n;
    }
    return e;
}

static VxExpr *parse_unary(Parser *p) {
    if (at(p, T_NOT)) {
        VxToken *t = peek(p); nexttok(p);
        VxExpr *r = parse_unary(p);
        if (!r) return NULL;
        VxExpr *e = new_expr(E_UNARY, t->line, t->col);
        if (!e) { vx_expr_free(r); return NULL; }
        e->u.unary.op = U_NOT; e->u.unary.rhs = r;
        return e;
    }
    if (at(p, T_MINUS)) {
        VxToken *t = peek(p); nexttok(p);
        VxExpr *r = parse_unary(p);
        if (!r) return NULL;
        VxExpr *e = new_expr(E_UNARY, t->line, t->col);
        if (!e) { vx_expr_free(r); return NULL; }
        e->u.unary.op = U_NEG; e->u.unary.rhs = r;
        return e;
    }
    return parse_summon(p);
}

static VxExpr *parse_mul(Parser *p) {
    VxExpr *l = parse_unary(p);
    if (!l) return NULL;
    for (;;) {
        VxBinOp op; bool is = true;
        if (at(p, T_STAR)) op = B_MUL;
        else if (at(p, T_SLASH)) op = B_DIV;
        else if (at(p, T_PERCENT)) op = B_MOD;
        else is = false;
        if (!is) return l;
        VxToken *t = peek(p); nexttok(p);
        VxExpr *r = parse_unary(p);
        if (!r) { vx_expr_free(l); return NULL; }
        VxExpr *e = new_expr(E_BINARY, t->line, t->col);
        if (!e) { vx_expr_free(l); vx_expr_free(r); return NULL; }
        e->u.binary.op = op; e->u.binary.lhs = l; e->u.binary.rhs = r;
        l = e;
    }
}

static VxExpr *parse_add(Parser *p) {
    VxExpr *l = parse_mul(p);
    if (!l) return NULL;
    for (;;) {
        VxBinOp op; bool is = true;
        if (at(p, T_PLUS)) op = B_ADD;
        else if (at(p, T_MINUS)) op = B_SUB;
        else is = false;
        if (!is) return l;
        VxToken *t = peek(p); nexttok(p);
        VxExpr *r = parse_mul(p);
        if (!r) { vx_expr_free(l); return NULL; }
        VxExpr *e = new_expr(E_BINARY, t->line, t->col);
        if (!e) { vx_expr_free(l); vx_expr_free(r); return NULL; }
        e->u.binary.op = op; e->u.binary.lhs = l; e->u.binary.rhs = r;
        l = e;
    }
}

static VxExpr *parse_cmp(Parser *p) {
    VxExpr *l = parse_add(p);
    if (!l) return NULL;
    for (;;) {
        VxBinOp op; bool is = true;
        if (at(p, T_GT)) op = B_GT;
        else if (at(p, T_LT)) op = B_LT;
        else if (at(p, T_GTE)) op = B_GTE;
        else if (at(p, T_LTE)) op = B_LTE;
        else is = false;
        if (!is) return l;
        VxToken *t = peek(p); nexttok(p);
        VxExpr *r = parse_add(p);
        if (!r) { vx_expr_free(l); return NULL; }
        VxExpr *e = new_expr(E_BINARY, t->line, t->col);
        if (!e) { vx_expr_free(l); vx_expr_free(r); return NULL; }
        e->u.binary.op = op; e->u.binary.lhs = l; e->u.binary.rhs = r;
        l = e;
    }
}

static VxExpr *parse_eq(Parser *p) {
    VxExpr *l = parse_cmp(p);
    if (!l) return NULL;
    for (;;) {
        VxBinOp op; bool is = true;
        if (at(p, T_EQEQ)) op = B_EQ;
        else if (at(p, T_NEQ)) op = B_NEQ;
        else is = false;
        if (!is) return l;
        VxToken *t = peek(p); nexttok(p);
        VxExpr *r = parse_cmp(p);
        if (!r) { vx_expr_free(l); return NULL; }
        VxExpr *e = new_expr(E_BINARY, t->line, t->col);
        if (!e) { vx_expr_free(l); vx_expr_free(r); return NULL; }
        e->u.binary.op = op; e->u.binary.lhs = l; e->u.binary.rhs = r;
        l = e;
    }
}

static VxExpr *parse_and(Parser *p) {
    VxExpr *l = parse_eq(p);
    if (!l) return NULL;
    while (at(p, T_AND)) {
        VxToken *t = peek(p); nexttok(p);
        VxExpr *r = parse_eq(p);
        if (!r) { vx_expr_free(l); return NULL; }
        VxExpr *e = new_expr(E_BINARY, t->line, t->col);
        if (!e) { vx_expr_free(l); vx_expr_free(r); return NULL; }
        e->u.binary.op = B_AND; e->u.binary.lhs = l; e->u.binary.rhs = r;
        l = e;
    }
    return l;
}

static VxExpr *parse_or(Parser *p) {
    VxExpr *l = parse_and(p);
    if (!l) return NULL;
    while (at(p, T_OR)) {
        VxToken *t = peek(p); nexttok(p);
        VxExpr *r = parse_and(p);
        if (!r) { vx_expr_free(l); return NULL; }
        VxExpr *e = new_expr(E_BINARY, t->line, t->col);
        if (!e) { vx_expr_free(l); vx_expr_free(r); return NULL; }
        e->u.binary.op = B_OR; e->u.binary.lhs = l; e->u.binary.rhs = r;
        l = e;
    }
    return l;
}

static VxExpr *parse_expr(Parser *p) {
    VxExpr *l = parse_or(p);
    if (!l) return NULL;
    while (at(p, T_QQ)) {
        VxToken *t = peek(p); nexttok(p);
        skip_nl(p);
        VxExpr *r = parse_or(p);
        if (!r) { vx_expr_free(l); return NULL; }
        VxExpr *e = new_expr(E_RESCUE, t->line, t->col);
        if (!e) { vx_expr_free(l); vx_expr_free(r); return NULL; }
        e->u.rescue.lhs = l; e->u.rescue.rhs = r;
        l = e;
    }
    return l;
}

/* parse beats until DOT/BANG/EOF (terminator left unconsumed) */
static VxBeat *parse_body(Parser *p) {
    VxBeat *head = NULL, *tail = NULL;
    for (;;) {
        skip_nl(p);
        if (at(p, T_DOT) || at(p, T_BANG) || at(p, T_EOF)) break;
        VxBeat *b = parse_beat(p);
        if (!b) { vx_beats_free(head); return NULL; }
        if (!head) head = tail = b;
        else { tail->next = b; tail = b; }
    }
    return head;
}

static VxBeat *parse_beat(Parser *p) {
    VxToken *t = peek(p);
    if (t->kind == T_AT) {
        int l = t->line, c = t->col;
        nexttok(p);
        if (!at(p, T_IDENT)) {
            vx_error_set(p->err, peek(p)->line, peek(p)->col, "after @ want a name");
            return NULL;
        }
        char *name = vx_strdup(peek(p)->lexeme);
        nexttok(p);
        if (!at(p, T_COLON)) {
            /* `@ Name` — place an installed operator on the bench */
            if (!expect_eol(p)) { free(name); return NULL; }
            VxBeat *b = new_beat(B_ATTACH, l, c);
            if (!b) { free(name); return NULL; }
            b->u.attach.name = name;
            return b;
        }
        nexttok(p); /* : */
        skip_nl(p);
        /* allow value on next line? no: require expr on same or next non-empty line */
        VxExpr *e = parse_expr(p);
        if (!e) { free(name); return NULL; }
        if (!expect_eol(p)) { free(name); vx_expr_free(e); return NULL; }
        VxBeat *b = new_beat(B_CELL, l, c);
        if (!b) { free(name); vx_expr_free(e); return NULL; }
        b->u.cell.name = name; b->u.cell.expr = e;
        return b;
    }
    if (t->kind == T_GT) {
        int l = t->line, c = t->col;
        nexttok(p);
        VxExpr *e = parse_expr(p);
        if (!e) return NULL;
        if (!expect_eol(p)) { vx_expr_free(e); return NULL; }
        VxBeat *b = new_beat(B_SHOW, l, c);
        if (!b) { vx_expr_free(e); return NULL; }
        b->u.show.expr = e;
        return b;
    }
    if (t->kind == T_QMARK) {
        int l = t->line, c = t->col;
        nexttok(p);
        VxExpr *cond = parse_expr(p);
        if (!cond) return NULL;
        if (!expect(p, T_COLON, ":")) { vx_expr_free(cond); return NULL; }
        if (!expect_eol(p)) { vx_expr_free(cond); return NULL; }
        VxBeat *thenb = parse_body(p);
        if (p->err->has) { vx_expr_free(cond); vx_beats_free(thenb); return NULL; }
        VxBeat *elseb = NULL;
        if (at(p, T_BANG)) {
            nexttok(p);
            if (!expect_eol(p)) { vx_expr_free(cond); vx_beats_free(thenb); return NULL; }
            elseb = parse_body(p);
            if (p->err->has) { vx_expr_free(cond); vx_beats_free(thenb); vx_beats_free(elseb); return NULL; }
        }
        if (!at(p, T_DOT)) {
            vx_error_set(p->err, peek(p)->line, peek(p)->col, "ask is not closed: want . on its own line");
            vx_expr_free(cond); vx_beats_free(thenb); vx_beats_free(elseb);
            return NULL;
        }
        nexttok(p);
        skip_nl(p);
        VxBeat *b = new_beat(B_ASK, l, c);
        if (!b) { vx_expr_free(cond); vx_beats_free(thenb); vx_beats_free(elseb); return NULL; }
        b->u.ask.cond = cond; b->u.ask.thenb = thenb; b->u.ask.elseb = elseb;
        return b;
    }
    if (t->kind == T_STAR) {
        int l = t->line, c = t->col;
        nexttok(p);
        VxExpr *cnt = parse_expr(p);
        if (!cnt) return NULL;
        if (!expect(p, T_COLON, ":")) { vx_expr_free(cnt); return NULL; }
        if (!expect_eol(p)) { vx_expr_free(cnt); return NULL; }
        VxBeat *body = parse_body(p);
        if (p->err->has) { vx_expr_free(cnt); vx_beats_free(body); return NULL; }
        if (!at(p, T_DOT)) {
            vx_error_set(p->err, peek(p)->line, peek(p)->col, "circle is not closed: want . on its own line");
            vx_expr_free(cnt); vx_beats_free(body);
            return NULL;
        }
        nexttok(p);
        skip_nl(p);
        VxBeat *b = new_beat(B_LOOP, l, c);
        if (!b) { vx_expr_free(cnt); vx_beats_free(body); return NULL; }
        b->u.loop.count = cnt; b->u.loop.body = body;
        return b;
    }
    if (t->kind == T_AMP) {
        int l = t->line, c = t->col;
        nexttok(p);
        if (!at(p, T_IDENT)) {
            vx_error_set(p->err, peek(p)->line, peek(p)->col, "after & want a tool name");
            return NULL;
        }
        char *name = vx_strdup(peek(p)->lexeme);
        nexttok(p);
        char **params = NULL;
        int nparams = 0, cap = 0;
        while (at(p, T_IDENT)) {
            if (nparams + 1 > cap) {
                int nc = cap ? cap * 2 : 4;
                char **ni = (char **)realloc(params, (size_t)nc * sizeof(char *));
                if (!ni) { free(name); for (int i = 0; i < nparams; i++) free(params[i]); free(params); return NULL; }
                params = ni; cap = nc;
            }
            params[nparams++] = vx_strdup(peek(p)->lexeme);
            nexttok(p);
        }
        if (!expect(p, T_COLON, ":")) {
            free(name); for (int i = 0; i < nparams; i++) free(params[i]); free(params);
            return NULL;
        }
        if (!expect_eol(p)) {
            free(name); for (int i = 0; i < nparams; i++) free(params[i]); free(params);
            return NULL;
        }
        VxBeat *body = parse_body(p);
        if (p->err->has) {
            free(name); for (int i = 0; i < nparams; i++) free(params[i]); free(params);
            vx_beats_free(body);
            return NULL;
        }
        if (!at(p, T_DOT)) {
            vx_error_set(p->err, peek(p)->line, peek(p)->col, "tool is not closed: want . on its own line");
            free(name); for (int i = 0; i < nparams; i++) free(params[i]); free(params);
            vx_beats_free(body);
            return NULL;
        }
        nexttok(p);
        skip_nl(p);
        VxBeat *b = new_beat(B_TOOL, l, c);
        if (!b) {
            free(name); for (int i = 0; i < nparams; i++) free(params[i]); free(params);
            vx_beats_free(body);
            return NULL;
        }
        b->u.tool.name = name; b->u.tool.params = params; b->u.tool.nparams = nparams;
        b->u.tool.body = body;
        return b;
    }
    if (t->kind == T_EQ) {
        int l = t->line, c = t->col;
        nexttok(p);
        VxExpr *e = parse_expr(p);
        if (!e) return NULL;
        if (!expect_eol(p)) { vx_expr_free(e); return NULL; }
        VxBeat *b = new_beat(B_GIVE, l, c);
        if (!b) { vx_expr_free(e); return NULL; }
        b->u.give.expr = e;
        return b;
    }
    if (t->kind == T_DOT || t->kind == T_BANG) {
        vx_error_set(p->err, t->line, t->col, "stray '%s' here", vx_tok_name(t->kind));
        return NULL;
    }
    /* expression beat */
    {
        int l = t->line, c = t->col;
        VxExpr *e = parse_expr(p);
        if (!e) return NULL;
        if (!expect_eol(p)) { vx_expr_free(e); return NULL; }
        VxBeat *b = new_beat(B_EXPR, l, c);
        if (!b) { vx_expr_free(e); return NULL; }
        b->u.expr.expr = e;
        return b;
    }
}

bool vx_parse(VxTokVec *toks, VxAst *out, VxError *err) {
    Parser p;
    p.t = toks; p.pos = 0; p.err = err;
    out->head = NULL;
    VxBeat *tail = NULL;
    skip_nl(&p);
    while (!at(&p, T_EOF)) {
        VxBeat *b = parse_beat(&p);
        if (!b) { vx_beats_free(out->head); out->head = NULL; return false; }
        if (!out->head) out->head = tail = b;
        else { tail->next = b; tail = b; }
        skip_nl(&p);
    }
    return true;
}
