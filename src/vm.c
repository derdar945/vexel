#include "vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern bool vx_vec_from_items(VxValue *out, VxValue *items, size_t n);

void vx_vm_init(VxVM *vm, VxProgram *prog) {
    memset(vm, 0, sizeof(*vm));
    vm->prog = prog;
}

void vx_vm_free(VxVM *vm) {
    if (!vm) return;
    if (vm->stack) {
        for (int i = 0; i < vm->sp; i++) vx_release(&vm->stack[i]);
        free(vm->stack);
    }
    if (vm->frames) {
        for (int i = 0; i < vm->nframes; i++) {
            VxFrame *f = &vm->frames[i];
            if (f->locals) {
                int nl = f->w ? f->w->nlocals : 0;
                for (int j = 0; j < nl; j++) vx_release(&f->locals[j]);
                free(f->locals);
            }
        }
        free(vm->frames);
    }
    free(vm->loops);
    if (vm->globals) {
        for (int i = 0; i < vm->prog->nglobals; i++) vx_release(&vm->globals[i]);
        free(vm->globals);
    }
    if (vm->ops) {
        for (int i = 0; i < vm->prog->nops; i++) vx_loader_unload(&vm->ops[i]);
        free(vm->ops);
    }
    memset(vm, 0, sizeof(*vm));
}

static bool push(VxVM *vm, VxValue v) {
    if (vm->sp + 1 > vm->capstack) {
        int nc = vm->capstack ? vm->capstack * 2 : 256;
        VxValue *nd = (VxValue *)realloc(vm->stack, (size_t)nc * sizeof(VxValue));
        if (!nd) return false;
        vm->stack = nd;
        vm->capstack = nc;
    }
    vm->stack[vm->sp++] = v; /* takes ownership */
    return true;
}

static VxValue pop(VxVM *vm) {
    return vm->stack[--vm->sp];
}

static bool push_frame(VxVM *vm, int tool, VxTool *w, int base) {
    if (vm->nframes + 1 > vm->capframes) {
        int nc = vm->capframes ? vm->capframes * 2 : 16;
        VxFrame *nd = (VxFrame *)realloc(vm->frames, (size_t)nc * sizeof(VxFrame));
        if (!nd) return false;
        vm->frames = nd;
        vm->capframes = nc;
    }
    VxFrame *f = &vm->frames[vm->nframes++];
    f->tool = tool;
    f->w = w;
    f->ip = 0;
    f->base = base;
    f->locals = NULL;
    f->loop_depth = vm->nloops;
    if (w->nlocals > 0) {
        f->locals = (VxValue *)calloc((size_t)w->nlocals, sizeof(VxValue));
        if (!f->locals) return false;
        for (int i = 0; i < w->nlocals; i++) f->locals[i] = vx_make_fail();
    }
    return true;
}

static void drop_frame(VxVM *vm) {
    VxFrame *f = &vm->frames[vm->nframes - 1];
    if (f->locals) {
        for (int i = 0; i < f->w->nlocals; i++) vx_release(&f->locals[i]);
        free(f->locals);
    }
    vm->nframes--;
}

static bool push_loop(VxVM *vm, long count) {
    if (vm->nloops + 1 > vm->caploops) {
        int nc = vm->caploops ? vm->caploops * 2 : 16;
        VxLoop *nd = (VxLoop *)realloc(vm->loops, (size_t)nc * sizeof(VxLoop));
        if (!nd) return false;
        vm->loops = nd;
        vm->caploops = nc;
    }
    vm->loops[vm->nloops].count = count;
    vm->loops[vm->nloops].it = 0;
    vm->nloops++;
    return true;
}

static VxValue native_call(int nv, VxValue *args, int argc) {
    (void)argc;
    switch (nv) {
        case VXN_LEN: {
            VxValue a = args[0];
            if (a.kind == VXK_TEXT) return vx_make_num((double)a.as.text->len);
            if (a.kind == VXK_VEC) return vx_make_num((double)a.as.vec->len);
            return vx_make_fail();
        }
        case VXN_AT: {
            VxValue c = args[0], ix = args[1];
            if (ix.kind != VXK_NUM) return vx_make_fail();
            long i = (long)ix.as.num;
            if (c.kind == VXK_VEC) {
                if (!c.as.vec || i < 0 || (size_t)i >= c.as.vec->len) return vx_make_fail();
                VxValue v = c.as.vec->items[i];
                vx_retain(&v);
                return v;
            }
            if (c.kind == VXK_TEXT) {
                if (!c.as.text || i < 0 || (size_t)i >= c.as.text->len) return vx_make_fail();
                return vx_make_text(&c.as.text->data[i], 1);
            }
            return vx_make_fail();
        }
        case VXN_TYPE: {
            const char *s = "fail";
            if (args[0].kind == VXK_NUM) s = "number";
            else if (args[0].kind == VXK_TEXT) s = "text";
            else if (args[0].kind == VXK_VEC) s = "vec";
            return vx_make_text_cstr(s);
        }
        case VXN_STR: {
            char *r = vx_repr(args[0]);
            if (!r) return vx_make_fail();
            VxValue v = vx_make_text_cstr(r);
            free(r);
            return v;
        }
        default: return vx_make_fail();
    }
}

static int run_until(VxVM *vm, int stop_depth, VxError *err);

int vx_vm_run(VxVM *vm, VxError *err) {
    if (vx_vm_start(vm, err)) return 1;
    return run_until(vm, 0, err);
}

/* summon a tool by name from inside an operator (event steps, timers).
 * Takes ownership of args (releases them). *out is a fresh owned value. */
int vx_vm_summon(VxVM *vm, const char *tool, VxOpVal **args, int argc,
                 VxOpVal **out, VxError *err) {
    VxProgram *p = vm->prog;
    *out = NULL;
    int ti = -1;
    for (int i = 0; i < p->ntools; i++) {
        if (strcmp(p->tools[i].name, tool) == 0) { ti = i; break; }
    }
    if (ti < 0) {
        vx_error_set(err, 0, 0, "operator summons unknown tool '%s'", tool);
        goto fail_args;
    }
    VxTool *w = &p->tools[ti];
    if (argc != w->nparams) {
        vx_error_set(err, 0, 0, "tool '%s' wants %d, summoned with %d", tool,
                     w->nparams, argc);
        goto fail_args;
    }
    if (vm->nframes >= 256) {
        vx_error_set(err, 0, 0, "tools nest too deep");
        goto fail_args;
    }
    {
        int base_sp = vm->sp;
        int base_depth = vm->nframes;
        int base_loops = vm->nloops;
        for (int i = 0; i < argc; i++) {
            if (!push(vm, *(VxValue *)args[i])) {
                vx_error_set(err, 0, 0, "out of memory");
                goto fail_args;
            }
            free(args[i]);
        }
        if (!push_frame(vm, ti, w, base_sp)) {
            vx_error_set(err, 0, 0, "out of memory");
            goto fail_args;
        }
        VxFrame *nf = &vm->frames[vm->nframes - 1];
        for (int i = 0; i < argc; i++) {
            vx_release(&nf->locals[i]);
            nf->locals[i] = vm->stack[base_sp + i];
            vx_retain(&nf->locals[i]);
        }
        for (int i = 0; i < argc; i++) {
            VxValue t = pop(vm);
            vx_release(&t);
        }
        if (run_until(vm, base_depth, err) != 0) {
            /* unwind whatever is left of this summon */
            while (vm->nframes > base_depth) drop_frame(vm);
            while (vm->sp > base_sp) {
                VxValue t = pop(vm);
                vx_release(&t);
            }
            vm->nloops = base_loops;
            return 1;
        }
        vm->nloops = base_loops;
        VxValue r;
        if (vm->sp > base_sp) {
            r = pop(vm);
            while (vm->sp > base_sp) {
                VxValue t = pop(vm);
                vx_release(&t);
            }
        } else {
            r = vx_make_fail();
        }
        VxValue *boxed = (VxValue *)malloc(sizeof(VxValue));
        if (!boxed) {
            vx_release(&r);
            vx_error_set(err, 0, 0, "out of memory");
            return 1;
        }
        *boxed = r;
        *out = (VxOpVal *)boxed;
        return 0;
    }
fail_args:
    if (args) {
        for (int i = 0; i < argc; i++) {
            vx_release((VxValue *)args[i]);
            free(args[i]);
        }
    }
    return 1;
}

/* one instruction. 0 = executed, 1 = runtime error. */
static int vm_step(VxVM *vm, VxError *err) {
    VxProgram *p = vm->prog;
    {
        VxFrame *f = &vm->frames[vm->nframes - 1];
        if (f->ip < 0 || f->ip >= f->w->ncode) {
            vx_error_set(err, 0, 0, "mark ran past the bench");
            return 1;
        }
        VxInstr in = f->w->code[f->ip++];
        switch ((VxOp)in.op) {
            case VX_CONST: {
                if (in.a < 0 || in.a >= p->nconsts) {
                    vx_error_set(err, 0, 0, "bad const mark");
                    return 1;
                }
                VxValue v = p->consts[in.a];
                vx_retain(&v);
                if (!push(vm, v)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_FAIL_OP: {
                if (!push(vm, vx_make_fail())) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_IT_OP: {
                if (vm->nloops <= 0) {
                    if (!push(vm, vx_make_fail())) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                } else {
                    if (!push(vm, vx_make_num((double)vm->loops[vm->nloops - 1].it))) {
                        vx_error_set(err, 0, 0, "out of memory"); return 1;
                    }
                }
                break;
            }
            case VX_LOAD_G: {
                if (in.a < 0 || in.a >= p->nglobals) { vx_error_set(err, 0, 0, "bad cell"); return 1; }
                VxValue v = vm->globals[in.a];
                vx_retain(&v);
                if (!push(vm, v)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_LOAD_L: {
                if (in.a < 0 || in.a >= f->w->nlocals || !f->locals) { vx_error_set(err, 0, 0, "bad bench cell"); return 1; }
                VxValue v = f->locals[in.a];
                vx_retain(&v);
                if (!push(vm, v)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_STORE_G: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue v = pop(vm);
                vx_release(&vm->globals[in.a]);
                vm->globals[in.a] = v;
                break;
            }
            case VX_STORE_L: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue v = pop(vm);
                vx_release(&f->locals[in.a]);
                f->locals[in.a] = v;
                break;
            }
            case VX_ADD: case VX_SUB: case VX_MUL: case VX_DIV: case VX_MOD:
            case VX_GT: case VX_LT: case VX_EQ: case VX_NEQ:
            case VX_GTE: case VX_LTE: case VX_AND: case VX_OR: {
                if (vm->sp < 2) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue b = pop(vm), a = pop(vm);
                VxValue r;
                switch ((VxOp)in.op) {
                    case VX_ADD: r = vx_add(a, b); break;
                    case VX_SUB: r = vx_sub(a, b); break;
                    case VX_MUL: r = vx_mul(a, b); break;
                    case VX_DIV: r = vx_div(a, b); break;
                    case VX_MOD: r = vx_mod(a, b); break;
                    case VX_GT: r = vx_cmp_gt(a, b); break;
                    case VX_LT: r = vx_cmp_lt(a, b); break;
                    case VX_EQ: r = vx_cmp_eq(a, b); break;
                    case VX_NEQ: r = vx_cmp_neq(a, b); break;
                    case VX_GTE: r = vx_cmp_gte(a, b); break;
                    case VX_LTE: r = vx_cmp_lte(a, b); break;
                    case VX_AND: r = vx_logic_and(a, b); break;
                    default: r = vx_logic_or(a, b); break;
                }
                vx_release(&a); vx_release(&b);
                if (!push(vm, r)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_NOT: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue a = pop(vm);
                VxValue r = vx_logic_not(a);
                vx_release(&a);
                if (!push(vm, r)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_NEG: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue a = pop(vm);
                VxValue r = vx_neg(a);
                vx_release(&a);
                if (!push(vm, r)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_VEC_OP: {
                int n = in.a;
                if (vm->sp < n) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue r;
                if (n == 0) { r = vx_make_vec(); }
                else {
                    VxValue *items = &vm->stack[vm->sp - n];
                    if (!vx_vec_from_items(&r, items, (size_t)n)) {
                        vx_error_set(err, 0, 0, "out of memory"); return 1;
                    }
                    for (int i = 0; i < n; i++) {
                        /* stack owned copies moved into vec; retain for vec */
                        vx_retain(&items[i]);
                    }
                    for (int i = 0; i < n; i++) {
                        VxValue t = pop(vm);
                        vx_release(&t);
                    }
                    /* vec already holds retained copies; fix: we retained then popped originals.
                       items[] pointed into freed region? Rebuild safely: */
                    /* Simpler correct path: copy first, then drop. Redo: */
                    /* (we already popped; r holds retained copies — but items[i] after realloc?
                       Actually vx_vec_from_items copied values (shallow), then we retained each,
                       then released originals -> net: vec owns one ref each. Correct.) */
                    if (!push(vm, r)) { vx_release(&r); vx_error_set(err, 0, 0, "out of memory"); return 1; }
                    break;
                }
                if (!push(vm, r)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_SHOW: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue v = pop(vm);
                char *r = vx_repr(v);
                const char *line = r ? r : "fail";
                if (vm->show_cb) vm->show_cb(line, vm->show_ud);
                else printf("%s\n", line);
                free(r);
                vx_release(&v);
                break;
            }
            case VX_JUMP: f->ip = in.a; break;
            case VX_JUMP_DRY: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue v = pop(vm);
                bool t = vx_is_true(v);
                vx_release(&v);
                if (!t) f->ip = in.a;
                break;
            }
            case VX_JUMP_NF: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                if (!vx_is_fail(vm->stack[vm->sp - 1])) f->ip = in.a;
                break;
            }
            case VX_DROP: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue v = pop(vm);
                vx_release(&v);
                break;
            }
            case VX_LOOP_ENTER: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue v = pop(vm);
                long n = vx_loop_count(v);
                vx_release(&v);
                if (n <= 0) { f->ip = in.a; }
                else {
                    int loops_before = vm->nloops;
                    if (!push_loop(vm, n)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                    (void)loops_before;
                }
                break;
            }
            case VX_LOOP_NEXT: {
                if (vm->nloops <= 0) { vx_error_set(err, 0, 0, "circle broke"); return 1; }
                VxLoop *lp = &vm->loops[vm->nloops - 1];
                lp->it++;
                if (lp->it < lp->count) f->ip = in.a;
                else vm->nloops--;
                break;
            }
            case VX_CALL: {
                int ti = in.a, argc = in.b;
                if (ti < 0 || ti >= p->ntools) { vx_error_set(err, 0, 0, "bad tool call"); return 1; }
                if (vm->sp < argc) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                if (vm->nframes >= 256) { vx_error_set(err, 0, 0, "tools nest too deep"); return 1; }
                VxTool *w = &p->tools[ti];
                int base = vm->sp - argc;
                if (!push_frame(vm, ti, w, base)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                VxFrame *nf = &vm->frames[vm->nframes - 1];
                for (int i = 0; i < argc && i < w->nparams; i++) {
                    vx_release(&nf->locals[i]);
                    nf->locals[i] = vm->stack[base + i];
                    /* move ownership: stack slot stays but will be truncated; retain for local */
                    vx_retain(&nf->locals[i]);
                }
                /* truncate args from stack (release originals kept in locals via retain) */
                for (int i = 0; i < argc; i++) {
                    VxValue t = pop(vm);
                    vx_release(&t);
                }
                f = &vm->frames[vm->nframes - 2]; /* refresh after realloc */
                (void)f;
                break;
            }
            case VX_NATIVE: {
                int nv = in.a, argc = in.b;
                if (vm->sp < argc) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue *args = &vm->stack[vm->sp - argc];
                VxValue r = native_call(nv, args, argc);
                for (int i = 0; i < argc; i++) {
                    VxValue t = pop(vm);
                    vx_release(&t);
                }
                if (!push(vm, r)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_GIVE: {
                if (vm->sp < 1) { vx_error_set(err, 0, 0, "bench is empty"); return 1; }
                VxValue r = pop(vm);
                int base = f->base;
                while (vm->sp > base) {
                    VxValue t = pop(vm);
                    vx_release(&t);
                }
                /* loops opened inside tool must die with it */
                /* circles opened inside this frame die with it:
                   an early = must not hijack outer NEXTs. */
                while (vm->nloops > f->loop_depth) vm->nloops--;
                drop_frame(vm);
                if (!push(vm, r)) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
                break;
            }
            case VX_HALT: {
                /* unwind main frame (and any stray circles) */
                while (vm->nloops > 0) vm->nloops--;
                drop_frame(vm);
                break;
            }
            case VX_ATTACH: {
                int oi = in.a;
                if (oi < 0 || oi >= p->nops) {
                    vx_error_set(err, 0, 0, "bad operator mark");
                    return 1;
                }
                if (!vm->ops) {
                    vm->ops = (VxLoadedOp *)calloc(
                        (size_t)(p->nops ? p->nops : 1), sizeof(VxLoadedOp));
                    if (!vm->ops) {
                        vx_error_set(err, 0, 0, "out of memory");
                        return 1;
                    }
                }
                if (!vm->ops[oi].handle) {
                    VxError lerr;
                    memset(&lerr, 0, sizeof(lerr));
                    if (!vx_loader_load(p->ops[oi].name, &vm->ops[oi],
                                        &lerr)) {
                        vx_error_set(err, 0, 0, "%s", lerr.msg);
                        return 1;
                    }
                }
                break;
            }
            case VX_CALL_OP: {
                int ri = in.a;
                if (ri < 0 || ri >= p->ncallrefs) {
                    vx_error_set(err, 0, 0, "bad operator summon");
                    return 1;
                }
                int oi = p->callrefs[ri].op;
                int fi = p->callrefs[ri].func;
                int argc = p->callrefs[ri].argc;
                if (!vm->ops || !vm->ops[oi].handle) {
                    vx_error_set(err, 0, 0,
                                 "operator not placed: '@ %s' first",
                                 p->ops[oi].name);
                    return 1;
                }
                if (vm->sp < argc) {
                    vx_error_set(err, 0, 0, "bench is empty");
                    return 1;
                }
                const VxOpInfo *info = vm->ops[oi].info;
                if (fi < 0 || fi >= info->nfuncs) {
                    vx_error_set(err, 0, 0, "operator changed its face");
                    return 1;
                }
                VxOpVal **av =
                    (VxOpVal **)malloc(sizeof(VxOpVal *) * (size_t)(argc ? argc : 1));
                if (!av) {
                    vx_error_set(err, 0, 0, "out of memory");
                    return 1;
                }
                for (int i = 0; i < argc; i++)
                    av[i] = (VxOpVal *)&vm->stack[vm->sp - argc + i];
                VxOpVal *out = NULL;
                void *prev_vm = vx_loader_swap_vm((void *)vm);
                int rc = info->funcs[fi].func(vx_op_api(), av, argc, &out);
                vx_loader_swap_vm(prev_vm);
                free(av);
                for (int i = 0; i < argc; i++) {
                    VxValue t = pop(vm);
                    vx_release(&t);
                }
                VxValue r;
                if (rc != 0 || !out) {
                    if (out) {
                        vx_release((VxValue *)out);
                        free(out);
                    }
                    r = vx_make_fail();
                } else {
                    r = *(VxValue *)out;
                    free(out);
                }
                if (!push(vm, r)) {
                    vx_error_set(err, 0, 0, "out of memory");
                    return 1;
                }
                break;
            }
            default:
                vx_error_set(err, 0, 0, "bad mark %d", in.op);
                return 1;
        }
    }
    return 0;
}

/* init a VM for stepping (what vx_vm_run did inline) */
int vx_vm_start(VxVM *vm, VxError *err) {
    VxProgram *p = vm->prog;
    if (p->nglobals > 0) {
        vm->globals = (VxValue *)calloc((size_t)p->nglobals, sizeof(VxValue));
        if (!vm->globals) { vx_error_set(err, 0, 0, "out of memory"); return 1; }
        for (int i = 0; i < p->nglobals; i++) vm->globals[i] = vx_make_fail();
    }
    if (!push_frame(vm, -1, &p->main, 0)) {
        vx_error_set(err, 0, 0, "out of memory");
        return 1;
    }
    return 0;
}

int vx_vm_live(VxVM *vm) {
    return vm->nframes > 0;
}

/* one public step: 1 = still running, 0 = finished, -1 = error */
int vx_vm_step(VxVM *vm, VxError *err) {
    if (!vx_vm_live(vm)) return 0;
    if (vm_step(vm, err)) return -1;
    return vx_vm_live(vm) ? 1 : 0;
}

static int run_until(VxVM *vm, int stop_depth, VxError *err) {
    while (vm->nframes > stop_depth) {
        if (vm_step(vm, err)) return 1;
    }
    return 0;
}
