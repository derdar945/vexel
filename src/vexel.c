#include "vexel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

const VxValue VX_FAIL_VAL = { VXK_FAIL, { .num = 0 } };

char *vx_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n + 1);
    return d;
}

char *vx_strndup(const char *s, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

void vx_error_set(VxError *e, int line, int col, const char *fmt, ...) {
    if (!e) return;
    e->has = true;
    e->line = line;
    e->col = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
    va_end(ap);
}

void vx_program_init(VxProgram *p) {
    memset(p, 0, sizeof(*p));
}

void vx_tool_free(VxTool *w) {
    if (!w) return;
    free(w->name);
    if (w->locals) {
        for (int i = 0; i < w->nlocals; i++) free(w->locals[i]);
        free(w->locals);
    }
    free(w->code);
    memset(w, 0, sizeof(*w));
}

void vx_program_free(VxProgram *p) {
    if (!p) return;
    if (p->globals) {
        for (int i = 0; i < p->nglobals; i++) free(p->globals[i]);
        free(p->globals);
    }
    if (p->consts) {
        for (int i = 0; i < p->nconsts; i++) vx_value_free(&p->consts[i]);
        free(p->consts);
    }
    if (p->tools) {
        for (int i = 0; i < p->ntools; i++) vx_tool_free(&p->tools[i]);
        free(p->tools);
    }
    if (p->ops) {
        for (int i = 0; i < p->nops; i++) free(p->ops[i].funcs);
        free(p->ops);
    }
    free(p->callrefs);
    vx_tool_free(&p->main);
    memset(p, 0, sizeof(*p));
}

int vx_program_find_global(VxProgram *p, const char *name) {
    for (int i = 0; i < p->nglobals; i++) {
        if (strcmp(p->globals[i], name) == 0) return i;
    }
    return -1;
}

int vx_program_add_global(VxProgram *p, const char *name) {
    int f = vx_program_find_global(p, name);
    if (f >= 0) return f;
    if (p->nglobals + 1 > p->cap_globals) {
        int nc = p->cap_globals ? p->cap_globals * 2 : 16;
        char **nd = (char **)realloc(p->globals, (size_t)nc * sizeof(char *));
        if (!nd) return -1;
        p->globals = nd;
        p->cap_globals = nc;
    }
    p->globals[p->nglobals] = vx_strdup(name);
    if (!p->globals[p->nglobals]) return -1;
    return p->nglobals++;
}

int vx_program_add_const(VxProgram *p, VxValue v) {
    if (p->nconsts + 1 > p->cap_consts) {
        int nc = p->cap_consts ? p->cap_consts * 2 : 32;
        VxValue *nd = (VxValue *)realloc(p->consts, (size_t)nc * sizeof(VxValue));
        if (!nd) return -1;
        p->consts = nd;
        p->cap_consts = nc;
    }
    p->consts[p->nconsts] = v;
    return p->nconsts++;
}

int vx_program_find_op(VxProgram *p, const char *name) {
    for (int i = 0; i < p->nops; i++) {
        if (strcmp(p->ops[i].name, name) == 0) return i;
    }
    return -1;
}

int vx_program_add_op(VxProgram *p, const char *name) {
    int f = vx_program_find_op(p, name);
    if (f >= 0) return f;
    if (p->nops + 1 > p->cap_ops) {
        int nc = p->cap_ops ? p->cap_ops * 2 : 4;
        VxOpUse *nd = (VxOpUse *)realloc(p->ops, (size_t)nc * sizeof(VxOpUse));
        if (!nd) return -1;
        p->ops = nd;
        p->cap_ops = nc;
    }
    memset(&p->ops[p->nops], 0, sizeof(VxOpUse));
    snprintf(p->ops[p->nops].name, sizeof(p->ops[p->nops].name), "%s", name);
    return p->nops++;
}

int vx_op_add_func(VxProgram *p, int opidx, const char *name, int arity) {
    VxOpUse *o = &p->ops[opidx];
    for (int i = 0; i < o->nfuncs; i++) {
        if (strcmp(o->funcs[i].name, name) == 0) return i;
    }
    VxOpFuncUse *nf = (VxOpFuncUse *)realloc(
        o->funcs, (size_t)(o->nfuncs + 1) * sizeof(VxOpFuncUse));
    if (!nf) return -1;
    o->funcs = nf;
    snprintf(o->funcs[o->nfuncs].name, sizeof(o->funcs[o->nfuncs].name), "%s",
             name);
    o->funcs[o->nfuncs].arity = arity;
    return o->nfuncs++;
}

int vx_program_add_callref(VxProgram *p, int op, int func, int argc) {
    if (p->ncallrefs + 1 > p->cap_callrefs) {
        int nc = p->cap_callrefs ? p->cap_callrefs * 2 : 16;
        VxOpCallRef *nd = (VxOpCallRef *)realloc(
            p->callrefs, (size_t)nc * sizeof(VxOpCallRef));
        if (!nd) return -1;
        p->callrefs = nd;
        p->cap_callrefs = nc;
    }
    p->callrefs[p->ncallrefs].op = op;
    p->callrefs[p->ncallrefs].func = func;
    p->callrefs[p->ncallrefs].argc = argc;
    return p->ncallrefs++;
}

int vx_tool_find_local(VxTool *w, const char *name) {
    for (int i = 0; i < w->nlocals; i++) {
        if (strcmp(w->locals[i], name) == 0) return i;
    }
    return -1;
}

int vx_tool_add_local(VxTool *w, const char *name) {
    int f = vx_tool_find_local(w, name);
    if (f >= 0) return f;
    if (w->nlocals + 1 > w->cap_locals) {
        int nc = w->cap_locals ? w->cap_locals * 2 : 8;
        char **nd = (char **)realloc(w->locals, (size_t)nc * sizeof(char *));
        if (!nd) return -1;
        w->locals = nd;
        w->cap_locals = nc;
    }
    w->locals[w->nlocals] = vx_strdup(name);
    if (!w->locals[w->nlocals]) return -1;
    return w->nlocals++;
}

void vx_tool_emit(VxTool *w, VxOp op, int32_t a, int32_t b) {
    if (w->ncode + 1 > w->capcode) {
        int nc = w->capcode ? w->capcode * 2 : 32;
        VxInstr *nd = (VxInstr *)realloc(w->code, (size_t)nc * sizeof(VxInstr));
        if (!nd) return;
        w->code = nd;
        w->capcode = nc;
    }
    w->code[w->ncode].op = (uint8_t)op;
    w->code[w->ncode].a = a;
    w->code[w->ncode].b = b;
    w->ncode++;
}

const char *vx_native_name(int n) {
    switch (n) {
        case VXN_LEN: return "len";
        case VXN_AT: return "at";
        case VXN_TYPE: return "type";
        case VXN_STR: return "str";
        default: return "?";
    }
}

int vx_native_lookup(const char *name) {
    if (strcmp(name, "len") == 0) return VXN_LEN;
    if (strcmp(name, "at") == 0) return VXN_AT;
    if (strcmp(name, "type") == 0) return VXN_TYPE;
    if (strcmp(name, "str") == 0) return VXN_STR;
    return -1;
}
