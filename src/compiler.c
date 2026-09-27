#include "compiler.h"
#include "opman.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct Ctx {
    VxProgram *prog;
    VxError *err;
    VxTool *cur;      /* chunk we emit into */
    bool in_tool;
} Ctx;

static int find_tool(VxProgram *p, const char *name) {
    for (int i = 0; i < p->ntools; i++) {
        if (strcmp(p->tools[i].name, name) == 0) return i;
    }
    return -1;
}

static const char *op_name(VxOp op);

const char *vx_op_name(int op) {
    return op_name((VxOp)op);
}

static const char *op_name(VxOp op) {
    switch (op) {
        case VX_CONST: return "CONST"; case VX_FAIL_OP: return "FAIL";
        case VX_IT_OP: return "IT"; case VX_LOAD_G: return "LOAD_G";
        case VX_LOAD_L: return "LOAD_L"; case VX_STORE_G: return "STORE_G";
        case VX_STORE_L: return "STORE_L"; case VX_ADD: return "ADD";
        case VX_SUB: return "SUB"; case VX_MUL: return "MUL";
        case VX_DIV: return "DIV"; case VX_MOD: return "MOD";
        case VX_GT: return "GT"; case VX_LT: return "LT";
        case VX_EQ: return "EQ"; case VX_NEQ: return "NEQ";
        case VX_GTE: return "GTE"; case VX_LTE: return "LTE";
        case VX_AND: return "AND"; case VX_OR: return "OR";
        case VX_NOT: return "NOT"; case VX_NEG: return "NEG";
        case VX_VEC_OP: return "VEC"; case VX_SHOW: return "SHOW";
        case VX_JUMP: return "JUMP"; case VX_JUMP_DRY: return "JUMP_DRY";
        case VX_JUMP_NF: return "JUMP_NF"; case VX_DROP: return "DROP";
        case VX_LOOP_ENTER: return "LOOP_ENTER"; case VX_LOOP_NEXT: return "LOOP_NEXT";
        case VX_CALL: return "CALL"; case VX_NATIVE: return "NATIVE";
        case VX_GIVE: return "GIVE"; case VX_HALT: return "HALT";
        case VX_ATTACH: return "ATTACH"; case VX_CALL_OP: return "CALL_OP";
        default: return "?";
    }
}

/* place installed operator on the bench; returns op index or -1 */
static int attach_op(Ctx *c, const char *name, int line, int col) {
    VxProgram *p = c->prog;
    int have = vx_program_find_op(p, name);
    if (have >= 0) return have;
    VxManifest m;
    VxError merr;
    memset(&merr, 0, sizeof(merr));
    if (!vx_opm_installed(name, &m, &merr)) {
        vx_error_set(c->err, line, col,
                     "unknown operator '@ %s' — install it first (!vex_add %s)",
                     name, name);
        return -1;
    }
    if (strcmp(m.type, "native") != 0) {
        vx_error_set(c->err, line, col,
                     "cannot place '%s' (type '%s' does not run yet)",
                     name, m.type);
        vx_manifest_free(&m);
        return -1;
    }
    int oi = vx_program_add_op(p, name);
    if (oi < 0) {
        vx_error_set(c->err, line, col, "out of memory");
        vx_manifest_free(&m);
        return -1;
    }
    for (int i = 0; i < m.nprovides; i++) {
        if (vx_op_add_func(p, oi, m.provides[i].name,
                           m.provides[i].arity) < 0) {
            vx_error_set(c->err, line, col, "out of memory");
            vx_manifest_free(&m);
            return -1;
        }
    }
    vx_manifest_free(&m);
    return oi;
}

static int find_opfunc(VxProgram *p, const char *name, int *opidx,
                       int *funcidx) {
    for (int o = 0; o < p->nops; o++) {
        for (int f = 0; f < p->ops[o].nfuncs; f++) {
            if (strcmp(p->ops[o].funcs[f].name, name) == 0) {
                if (opidx) *opidx = o;
                if (funcidx) *funcidx = f;
                return 1;
            }
        }
    }
    return 0;
}

/* try to evaluate a pure-number subtree at compile time */
static int fold_num(const VxExpr *e, double *out) {
    if (!e) return 0;
    if (e->kind == E_NUM) {
        *out = e->u.num;
        return 1;
    }
    if (e->kind == E_UNARY) {
        double r = 0;
        if (!fold_num(e->u.unary.rhs, &r)) return 0;
        VxValue v = vx_make_num(r);
        VxValue q = (e->u.unary.op == U_NOT) ? vx_logic_not(v) : vx_neg(v);
        *out = q.as.num;
        return 1;
    }
    if (e->kind == E_BINARY) {
        double l = 0, r = 0;
        if (!fold_num(e->u.binary.lhs, &l)) return 0;
        if (!fold_num(e->u.binary.rhs, &r)) return 0;
        VxValue a = vx_make_num(l), b = vx_make_num(r), q = vx_make_fail();
        switch (e->u.binary.op) {
            case B_ADD: q = vx_add(a, b); break;
            case B_SUB: q = vx_sub(a, b); break;
            case B_MUL: q = vx_mul(a, b); break;
            case B_DIV: q = vx_div(a, b); break;
            case B_MOD: q = vx_mod(a, b); break;
            case B_GT: q = vx_cmp_gt(a, b); break;
            case B_LT: q = vx_cmp_lt(a, b); break;
            case B_GTE: q = vx_cmp_gte(a, b); break;
            case B_LTE: q = vx_cmp_lte(a, b); break;
            case B_EQ: q = vx_cmp_eq(a, b); break;
            case B_NEQ: q = vx_cmp_neq(a, b); break;
            case B_AND: q = vx_logic_and(a, b); break;
            case B_OR: q = vx_logic_or(a, b); break;
        }
        if (q.kind != VXK_NUM) return 0; /* fail stays runtime */
        *out = q.as.num;
        return 1;
    }
    return 0;
}

static bool compile_expr(Ctx *c, VxExpr *e);
static bool compile_beats(Ctx *c, VxBeat *b, bool top);

static bool compile_expr(Ctx *c, VxExpr *e) {
    VxProgram *p = c->prog;
    switch (e->kind) {
        case E_NUM: {
            int k = vx_program_add_const(p, vx_make_num(e->u.num));
            if (k < 0) { vx_error_set(c->err, e->line, e->col, "out of memory"); return false; }
            vx_tool_emit(c->cur, VX_CONST, k, 0);
            return true;
        }
        case E_TEXT: {
            int k = vx_program_add_const(p, vx_make_text_cstr(e->u.text));
            if (k < 0) { vx_error_set(c->err, e->line, e->col, "out of memory"); return false; }
            vx_tool_emit(c->cur, VX_CONST, k, 0);
            return true;
        }
        case E_FAIL:
            vx_tool_emit(c->cur, VX_FAIL_OP, 0, 0);
            return true;
        case E_IT:
            vx_tool_emit(c->cur, VX_IT_OP, 0, 0);
            return true;
        case E_VAR: {
            const char *nm = e->u.varname;
            if (c->in_tool) {
                int s = vx_tool_find_local(c->cur, nm);
                if (s >= 0) { vx_tool_emit(c->cur, VX_LOAD_L, s, 0); return true; }
            }
            int g = vx_program_find_global(p, nm);
            if (g >= 0) { vx_tool_emit(c->cur, VX_LOAD_G, g, 0); return true; }
            vx_error_set(c->err, e->line, e->col, "unknown cell '@ %s' — forge it first", nm);
            return false;
        }
        case E_UNARY: {
            double f = 0;
            if (fold_num(e, &f)) {
                int k = vx_program_add_const(p, vx_make_num(f));
                if (k < 0) { vx_error_set(c->err, e->line, e->col, "out of memory"); return false; }
                vx_tool_emit(c->cur, VX_CONST, k, 0);
                return true;
            }
            if (!compile_expr(c, e->u.unary.rhs)) return false;
            vx_tool_emit(c->cur, e->u.unary.op == U_NOT ? VX_NOT : VX_NEG, 0, 0);
            return true;
        }
        case E_BINARY: {
            double f = 0;
            if (fold_num(e, &f)) {
                int k = vx_program_add_const(p, vx_make_num(f));
                if (k < 0) { vx_error_set(c->err, e->line, e->col, "out of memory"); return false; }
                vx_tool_emit(c->cur, VX_CONST, k, 0);
                return true;
            }
            if (!compile_expr(c, e->u.binary.lhs)) return false;
            if (!compile_expr(c, e->u.binary.rhs)) return false;
            VxOp op = VX_ADD;
            switch (e->u.binary.op) {
                case B_ADD: op = VX_ADD; break;
                case B_SUB: op = VX_SUB; break;
                case B_MUL: op = VX_MUL; break;
                case B_DIV: op = VX_DIV; break;
                case B_MOD: op = VX_MOD; break;
                case B_GT: op = VX_GT; break;
                case B_LT: op = VX_LT; break;
                case B_GTE: op = VX_GTE; break;
                case B_LTE: op = VX_LTE; break;
                case B_EQ: op = VX_EQ; break;
                case B_NEQ: op = VX_NEQ; break;
                case B_AND: op = VX_AND; break;
                case B_OR: op = VX_OR; break;
            }
            vx_tool_emit(c->cur, op, 0, 0);
            return true;
        }
        case E_RESCUE: {
            if (!compile_expr(c, e->u.rescue.lhs)) return false;
            int jpos = c->cur->ncode;
            vx_tool_emit(c->cur, VX_JUMP_NF, -1, 0);
            vx_tool_emit(c->cur, VX_DROP, 0, 0);
            if (!compile_expr(c, e->u.rescue.rhs)) return false;
            c->cur->code[jpos].a = c->cur->ncode;
            return true;
        }
        case E_VEC: {
            for (int i = 0; i < e->u.vec.nitems; i++) {
                if (!compile_expr(c, e->u.vec.items[i])) return false;
            }
            vx_tool_emit(c->cur, VX_VEC_OP, e->u.vec.nitems, 0);
            return true;
        }
        case E_CALL: {
            /* Op.verb # ... — summon into one named operator.
               Unqualified keeps the old rule: the first attached
               operator that provides the verb wins. */
            if (e->u.call.op) {
                int oi = vx_program_find_op(p, e->u.call.op);
                if (oi < 0) {
                    vx_error_set(c->err, e->line, e->col,
                        "summon '%s.%s': place @ %s first",
                        e->u.call.op, e->u.call.name, e->u.call.op);
                    return false;
                }
                int fi = -1;
                for (int f = 0; f < p->ops[oi].nfuncs; f++) {
                    if (strcmp(p->ops[oi].funcs[f].name, e->u.call.name) == 0) {
                        fi = f;
                        break;
                    }
                }
                if (fi < 0) {
                    vx_error_set(c->err, e->line, e->col,
                        "'%s' has no verb '%s'",
                        e->u.call.op, e->u.call.name);
                    return false;
                }
                int want = p->ops[oi].funcs[fi].arity;
                if (e->u.call.nargs != want) {
                    vx_error_set(c->err, e->line, e->col,
                        "'%s.%s' wants %d, summoned with %d",
                        e->u.call.op, e->u.call.name, want,
                        e->u.call.nargs);
                    return false;
                }
                for (int i = 0; i < e->u.call.nargs; i++) {
                    if (!compile_expr(c, e->u.call.args[i])) return false;
                }
                int ref = vx_program_add_callref(p, oi, fi,
                                                 e->u.call.nargs);
                if (ref < 0) {
                    vx_error_set(c->err, e->line, e->col, "out of memory");
                    return false;
                }
                vx_tool_emit(c->cur, VX_CALL_OP, ref, 0);
                return true;
            }
            int ti = find_tool(p, e->u.call.name);
            if (ti >= 0) {
                int want = p->tools[ti].nparams;
                if (e->u.call.nargs != want) {
                    vx_error_set(c->err, e->line, e->col,
                        "tool '%s' wants %d, summoned with %d",
                        e->u.call.name, want, e->u.call.nargs);
                    return false;
                }
                for (int i = 0; i < e->u.call.nargs; i++) {
                    if (!compile_expr(c, e->u.call.args[i])) return false;
                }
                vx_tool_emit(c->cur, VX_CALL, ti, e->u.call.nargs);
                return true;
            }
            int oi = -1, fi = -1;
            if (find_opfunc(p, e->u.call.name, &oi, &fi)) {
                int want = p->ops[oi].funcs[fi].arity;
                if (e->u.call.nargs != want) {
                    vx_error_set(c->err, e->line, e->col,
                        "'%s' wants %d, summoned with %d",
                        e->u.call.name, want, e->u.call.nargs);
                    return false;
                }
                for (int i = 0; i < e->u.call.nargs; i++) {
                    if (!compile_expr(c, e->u.call.args[i])) return false;
                }
                int ref = vx_program_add_callref(p, oi, fi,
                                                 e->u.call.nargs);
                if (ref < 0) {
                    vx_error_set(c->err, e->line, e->col, "out of memory");
                    return false;
                }
                vx_tool_emit(c->cur, VX_CALL_OP, ref, 0);
                return true;
            }
            int nv = vx_native_lookup(e->u.call.name);
            if (nv >= 0) {
                int want = (nv == VXN_AT) ? 2 : 1;
                if (e->u.call.nargs != want) {
                    vx_error_set(c->err, e->line, e->col,
                        "'%s' wants %d, summoned with %d",
                        e->u.call.name, want, e->u.call.nargs);
                    return false;
                }
                for (int i = 0; i < e->u.call.nargs; i++) {
                    if (!compile_expr(c, e->u.call.args[i])) return false;
                }
                vx_tool_emit(c->cur, VX_NATIVE, nv, e->u.call.nargs);
                return true;
            }
            vx_error_set(c->err, e->line, e->col, "unknown tool '%s'", e->u.call.name);
            return false;
        }
        default:
            vx_error_set(c->err, e->line, e->col, "bad expression");
            return false;
    }
}

static bool compile_beats(Ctx *c, VxBeat *b, bool top) {
    for (; b; b = b->next) {
        switch (b->kind) {
            case B_CELL: {
                if (!compile_expr(c, b->u.cell.expr)) return false;
                if (c->in_tool) {
                    int s = vx_tool_add_local(c->cur, b->u.cell.name);
                    if (s < 0) { vx_error_set(c->err, b->line, b->col, "out of memory"); return false; }
                    vx_tool_emit(c->cur, VX_STORE_L, s, 0);
                } else {
                    int g = vx_program_add_global(c->prog, b->u.cell.name);
                    if (g < 0) { vx_error_set(c->err, b->line, b->col, "out of memory"); return false; }
                    vx_tool_emit(c->cur, VX_STORE_G, g, 0);
                }
                break;
            }
            case B_SHOW: {
                if (!compile_expr(c, b->u.show.expr)) return false;
                vx_tool_emit(c->cur, VX_SHOW, 0, 0);
                break;
            }
            case B_EXPR: {
                if (!compile_expr(c, b->u.expr.expr)) return false;
                vx_tool_emit(c->cur, VX_DROP, 0, 0);
                break;
            }
            case B_GIVE: {
                if (!c->in_tool) {
                    vx_error_set(c->err, b->line, b->col, "'=' lives only inside a tool");
                    return false;
                }
                if (!compile_expr(c, b->u.give.expr)) return false;
                vx_tool_emit(c->cur, VX_GIVE, 0, 0);
                break;
            }
            case B_ASK: {
                if (!compile_expr(c, b->u.ask.cond)) return false;
                int jdry = c->cur->ncode;
                vx_tool_emit(c->cur, VX_JUMP_DRY, -1, 0);
                if (!compile_beats(c, b->u.ask.thenb, false)) return false;
                int jend = c->cur->ncode;
                vx_tool_emit(c->cur, VX_JUMP, -1, 0);
                c->cur->code[jdry].a = c->cur->ncode;
                if (b->u.ask.elseb) {
                    if (!compile_beats(c, b->u.ask.elseb, false)) return false;
                }
                c->cur->code[jend].a = c->cur->ncode;
                break;
            }
            case B_LOOP: {
                if (!compile_expr(c, b->u.loop.count)) return false;
                int jenter = c->cur->ncode;
                vx_tool_emit(c->cur, VX_LOOP_ENTER, -1, 0);
                int start = c->cur->ncode;
                if (!compile_beats(c, b->u.loop.body, false)) return false;
                vx_tool_emit(c->cur, VX_LOOP_NEXT, start, 0);
                c->cur->code[jenter].a = c->cur->ncode;
                break;
            }
            case B_TOOL: {
                if (!top) {
                    vx_error_set(c->err, b->line, b->col, "tool must stand on its own (top level)");
                    return false;
                }
                if (find_tool(c->prog, b->u.tool.name) >= 0 ||
                    vx_native_lookup(b->u.tool.name) >= 0) {
                    /* allow shadowing native? we said user wins, but redefinition no.
                       if native exists and no user tool yet, allow first definition */
                    if (find_tool(c->prog, b->u.tool.name) >= 0) {
                        vx_error_set(c->err, b->line, b->col, "tool '%s' is already forged", b->u.tool.name);
                        return false;
                    }
                }
                if (c->prog->ntools + 1 > 128) {
                    vx_error_set(c->err, b->line, b->col, "too many tools");
                    return false;
                }
                if (c->prog->ntools + 1 > c->prog->cap_tools) {
                    int nc = c->prog->cap_tools ? c->prog->cap_tools * 2 : 8;
                    VxTool *nd = (VxTool *)realloc(c->prog->tools, (size_t)nc * sizeof(VxTool));
                    if (!nd) { vx_error_set(c->err, b->line, b->col, "out of memory"); return false; }
                    c->prog->tools = nd;
                    c->prog->cap_tools = nc;
                }
                VxTool *nt = &c->prog->tools[c->prog->ntools];
                memset(nt, 0, sizeof(*nt));
                nt->name = vx_strdup(b->u.tool.name);
                if (!nt->name) { vx_error_set(c->err, b->line, b->col, "out of memory"); return false; }
                for (int i = 0; i < b->u.tool.nparams; i++) {
                    if (vx_tool_find_local(nt, b->u.tool.params[i]) >= 0) {
                        vx_error_set(c->err, b->line, b->col, "doubled mark '%s'", b->u.tool.params[i]);
                        vx_tool_free(nt);
                        memset(nt, 0, sizeof(*nt));
                        return false;
                    }
                    if (vx_tool_add_local(nt, b->u.tool.params[i]) < 0) {
                        vx_error_set(c->err, b->line, b->col, "out of memory");
                        vx_tool_free(nt);
                        memset(nt, 0, sizeof(*nt));
                        return false;
                    }
                }
                nt->nparams = b->u.tool.nparams;
                c->prog->ntools++;
                /* compile body into new tool */
                VxTool *prev = c->cur;
                bool prev_in = c->in_tool;
                c->cur = nt;
                c->in_tool = true;
                bool ok = compile_beats(c, b->u.tool.body, false);
                if (ok) {
                    vx_tool_emit(nt, VX_FAIL_OP, 0, 0);
                    vx_tool_emit(nt, VX_GIVE, 0, 0);
                }
                c->cur = prev;
                c->in_tool = prev_in;
                if (!ok) return false;
                break;
            }
            case B_ATTACH: {
                if (!top) {
                    vx_error_set(c->err, b->line, b->col, "operator must be placed on its own (top level)");
                    return false;
                }
                int oi = attach_op(c, b->u.attach.name, b->line, b->col);
                if (oi < 0) return false;
                vx_tool_emit(c->cur, VX_ATTACH, oi, 0);
                break;
            }
        }
    }
    return true;
}

bool vx_compile(VxAst *ast, VxProgram *out, VxError *err) {
    vx_program_init(out);
    Ctx c;
    c.prog = out;
    c.err = err;
    c.cur = &out->main;
    c.in_tool = false;
    out->main.name = vx_strdup("main");
    if (!out->main.name) { vx_error_set(err, 1, 1, "out of memory"); return false; }
    if (!compile_beats(&c, ast->head, true)) {
        vx_program_free(out);
        return false;
    }
    vx_tool_emit(&out->main, VX_HALT, 0, 0);
    return true;
}

void vx_disasm(const VxProgram *p) {
    printf("; globals %d, consts %d, tools %d, ops %d\n", p->nglobals, p->nconsts, p->ntools, p->nops);
    for (int i = 0; i < p->nglobals; i++) printf("; g%d = %s\n", i, p->globals[i]);
    for (int i = 0; i < p->nconsts; i++) {
        char *r = vx_repr(p->consts[i]);
        printf("; k%d = %s\n", i, r ? r : "?");
        free(r);
    }
    for (int i = 0; i < p->ntools; i++) {
        const VxTool *w = &p->tools[i];
        printf("; tool %d '%s' params=%d locals=%d\n", i, w->name, w->nparams, w->nlocals);
        for (int j = 0; j < w->nlocals; j++)
            printf(";   l%d = %s%s\n", j, w->locals[j], j < w->nparams ? " (mark)" : "");
        for (int j = 0; j < w->ncode; j++)
            printf("  t%d:%04d %-10s %d %d\n", i, j, op_name((VxOp)w->code[j].op), w->code[j].a, w->code[j].b);
    }
    printf("; main:\n");
    for (int j = 0; j < p->main.ncode; j++)
        printf("  m:%04d %-10s %d %d\n", j, op_name((VxOp)p->main.code[j].op), p->main.code[j].a, p->main.code[j].b);
    for (int i = 0; i < p->nops; i++) {
        printf("; op %d '%s':\n", i, p->ops[i].name);
        for (int j = 0; j < p->ops[i].nfuncs; j++)
            printf(";   %s # (%d)\n", p->ops[i].funcs[j].name,
                   p->ops[i].funcs[j].arity);
    }
}

/* ---- .vxb ---- */
static void w32(FILE *f, uint32_t v) {
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
    fwrite(b, 1, 4, f);
}
static bool r32(FILE *f, uint32_t *v) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static void write_value(FILE *f, VxValue v) {
    fputc((int)v.kind, f);
    if (v.kind == VXK_NUM) {
        fwrite(&v.as.num, sizeof(double), 1, f);
    } else if (v.kind == VXK_TEXT) {
        w32(f, (uint32_t)v.as.text->len);
        if (v.as.text->len) fwrite(v.as.text->data, 1, v.as.text->len, f);
    }
}

static bool read_value(FILE *f, VxValue *out) {
    int k = fgetc(f);
    if (k == EOF) return false;
    if (k == VXK_FAIL) { *out = vx_make_fail(); return true; }
    if (k == VXK_NUM) {
        double d = 0;
        if (fread(&d, sizeof(double), 1, f) != 1) return false;
        *out = vx_make_num(d);
        return true;
    }
    if (k == VXK_TEXT) {
        uint32_t L = 0;
        if (!r32(f, &L)) return false;
        char *buf = (char *)malloc(L ? L : 1);
        if (!buf) return false;
        if (L && fread(buf, 1, L, f) != L) { free(buf); return false; }
        *out = vx_make_text(buf, L);
        free(buf);
        return true;
    }
    return false;
}

bool vx_save(const VxProgram *p, const char *path, VxError *err) {
    FILE *f = fopen(path, "wb");
    if (!f) { vx_error_set(err, 0, 0, "cannot write %s", path); return false; }
    fwrite(VEXEL_MAGIC2, 1, 4, f);
    w32(f, (uint32_t)p->nglobals);
    for (int i = 0; i < p->nglobals; i++) {
        uint32_t L = (uint32_t)strlen(p->globals[i]);
        w32(f, L);
        fwrite(p->globals[i], 1, L, f);
    }
    w32(f, (uint32_t)p->nconsts);
    for (int i = 0; i < p->nconsts; i++) write_value(f, p->consts[i]);
    w32(f, (uint32_t)p->ntools);
    for (int i = 0; i < p->ntools; i++) {
        const VxTool *w = &p->tools[i];
        uint32_t L = (uint32_t)strlen(w->name);
        w32(f, L); fwrite(w->name, 1, L, f);
        w32(f, (uint32_t)w->nparams);
        w32(f, (uint32_t)w->nlocals);
        for (int j = 0; j < w->nlocals; j++) {
            uint32_t Ll = (uint32_t)strlen(w->locals[j]);
            w32(f, Ll); fwrite(w->locals[j], 1, Ll, f);
        }
        w32(f, (uint32_t)w->ncode);
        for (int j = 0; j < w->ncode; j++) {
            fputc(w->code[j].op, f);
            w32(f, (uint32_t)w->code[j].a);
            w32(f, (uint32_t)w->code[j].b);
        }
    }
    w32(f, (uint32_t)p->main.ncode);
    for (int j = 0; j < p->main.ncode; j++) {
        fputc(p->main.code[j].op, f);
        w32(f, (uint32_t)p->main.code[j].a);
        w32(f, (uint32_t)p->main.code[j].b);
    }
    /* VXB2: operators + callrefs */
    w32(f, (uint32_t)p->nops);
    for (int i = 0; i < p->nops; i++) {
        uint32_t L = (uint32_t)strlen(p->ops[i].name);
        w32(f, L);
        fwrite(p->ops[i].name, 1, L, f);
        w32(f, (uint32_t)p->ops[i].nfuncs);
        for (int j = 0; j < p->ops[i].nfuncs; j++) {
            uint32_t Lf = (uint32_t)strlen(p->ops[i].funcs[j].name);
            w32(f, Lf);
            fwrite(p->ops[i].funcs[j].name, 1, Lf, f);
            w32(f, (uint32_t)p->ops[i].funcs[j].arity);
        }
    }
    w32(f, (uint32_t)p->ncallrefs);
    for (int i = 0; i < p->ncallrefs; i++) {
        w32(f, (uint32_t)p->callrefs[i].op);
        w32(f, (uint32_t)p->callrefs[i].func);
        w32(f, (uint32_t)p->callrefs[i].argc);
    }
    fclose(f);
    return true;
}

bool vx_load(const char *path, VxProgram *out, VxError *err) {
    FILE *f = fopen(path, "rb");
    if (!f) { vx_error_set(err, 0, 0, "cannot read %s", path); return false; }
    vx_program_init(out);
    char mg[4];
    if (fread(mg, 1, 4, f) != 4 ||
        (memcmp(mg, VEXEL_MAGIC, 4) != 0 &&
         memcmp(mg, VEXEL_MAGIC2, 4) != 0)) {
        fclose(f);
        vx_error_set(err, 0, 0, "not a .vxb file");
        return false;
    }
    int v2 = memcmp(mg, VEXEL_MAGIC2, 4) == 0;
    uint32_t ng = 0;
    if (!r32(f, &ng)) goto bad;
    for (uint32_t i = 0; i < ng; i++) {
        uint32_t L = 0;
        if (!r32(f, &L) || L > 100000) goto bad;
        char *s = (char *)malloc(L + 1);
        if (!s) goto bad;
        if (L && fread(s, 1, L, f) != L) { free(s); goto bad; }
        s[L] = '\0';
        int g = vx_program_add_global(out, s);
        free(s);
        if (g < 0) goto bad;
    }
    uint32_t nc = 0;
    if (!r32(f, &nc)) goto bad;
    for (uint32_t i = 0; i < nc; i++) {
        VxValue v;
        if (!read_value(f, &v)) goto bad;
        if (vx_program_add_const(out, v) < 0) { vx_release(&v); goto bad; }
    }
    uint32_t nt = 0;
    if (!r32(f, &nt)) goto bad;
    for (uint32_t i = 0; i < nt; i++) {
        uint32_t L = 0;
        if (!r32(f, &L) || L > 100000) goto bad;
        char *nm = (char *)malloc(L + 1);
        if (!nm) goto bad;
        if (L && fread(nm, 1, L, f) != L) { free(nm); goto bad; }
        nm[L] = '\0';
        uint32_t nparams = 0, nlocals = 0;
        if (!r32(f, &nparams) || !r32(f, &nlocals)) { free(nm); goto bad; }
        if (out->ntools + 1 > out->cap_tools) {
            int ncap = out->cap_tools ? out->cap_tools * 2 : 8;
            VxTool *nd = (VxTool *)realloc(out->tools, (size_t)ncap * sizeof(VxTool));
            if (!nd) { free(nm); goto bad; }
            out->tools = nd; out->cap_tools = ncap;
        }
        VxTool *w = &out->tools[out->ntools];
        memset(w, 0, sizeof(*w));
        w->name = nm;
        w->nparams = (int)nparams;
        for (uint32_t j = 0; j < nlocals; j++) {
            uint32_t Ll = 0;
            if (!r32(f, &Ll) || Ll > 100000) goto bad;
            char *ln = (char *)malloc(Ll + 1);
            if (!ln) goto bad;
            if (Ll && fread(ln, 1, Ll, f) != Ll) { free(ln); goto bad; }
            ln[Ll] = '\0';
            if (vx_tool_add_local(w, ln) < 0) { free(ln); goto bad; }
            free(ln);
        }
        out->ntools++;
        uint32_t ncode = 0;
        if (!r32(f, &ncode) || ncode > 1000000) goto bad;
        for (uint32_t j = 0; j < ncode; j++) {
            int op = fgetc(f);
            uint32_t a = 0, b = 0;
            if (op == EOF || !r32(f, &a) || !r32(f, &b)) goto bad;
            vx_tool_emit(w, (VxOp)op, (int32_t)a, (int32_t)b);
        }
    }
    {
        uint32_t ncode = 0;
        out->main.name = vx_strdup("main");
        if (!r32(f, &ncode) || ncode > 1000000) goto bad;
        for (uint32_t j = 0; j < ncode; j++) {
            int op = fgetc(f);
            uint32_t a = 0, b = 0;
            if (op == EOF || !r32(f, &a) || !r32(f, &b)) goto bad;
            vx_tool_emit(&out->main, (VxOp)op, (int32_t)a, (int32_t)b);
        }
    }
    if (v2) {
        uint32_t nops = 0;
        if (!r32(f, &nops) || nops > 64) goto bad;
        for (uint32_t i = 0; i < nops; i++) {
            uint32_t L = 0;
            if (!r32(f, &L) || L == 0 || L > 63) goto bad;
            char nm[64];
            if (fread(nm, 1, L, f) != L) goto bad;
            nm[L] = '\0';
            int oi = vx_program_add_op(out, nm);
            if (oi < 0) goto bad;
            uint32_t nf = 0;
            if (!r32(f, &nf) || nf > 256) goto bad;
            for (uint32_t j = 0; j < nf; j++) {
                uint32_t Lf = 0, ar = 0;
                if (!r32(f, &Lf) || Lf == 0 || Lf > 63) goto bad;
                char fn[64];
                if (fread(fn, 1, Lf, f) != Lf) goto bad;
                fn[Lf] = '\0';
                if (!r32(f, &ar) || ar > 16) goto bad;
                if (vx_op_add_func(out, oi, fn, (int)ar) < 0) goto bad;
            }
        }
        uint32_t nr = 0;
        if (!r32(f, &nr) || nr > 100000) goto bad;
        for (uint32_t i = 0; i < nr; i++) {
            uint32_t o = 0, fn2 = 0, ac = 0;
            if (!r32(f, &o) || !r32(f, &fn2) || !r32(f, &ac)) goto bad;
            if (vx_program_add_callref(out, (int)o, (int)fn2, (int)ac) < 0)
                goto bad;
        }
    }
    fclose(f);
    return true;
bad:
    fclose(f);
    vx_program_free(out);
    vx_error_set(err, 0, 0, "broken .vxb file");
    return false;
}
