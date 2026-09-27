#include "vexel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

VxValue vx_make_num(double n) {
    VxValue v; v.kind = VXK_NUM; v.as.num = n; return v;
}

VxValue vx_make_fail(void) {
    VxValue v; v.kind = VXK_FAIL; v.as.num = 0; return v;
}

VxValue vx_make_text(const char *s, size_t len) {
    VxText *t = (VxText *)malloc(sizeof(VxText) + len + 1);
    if (!t) return vx_make_fail();
    t->refs = 1;
    t->len = len;
    memcpy(t->data, s, len);
    t->data[len] = '\0';
    VxValue v; v.kind = VXK_TEXT; v.as.text = t; return v;
}

VxValue vx_make_text_cstr(const char *s) {
    if (!s) return vx_make_fail();
    return vx_make_text(s, strlen(s));
}

VxValue vx_make_vec(void) {
    VxVec *c = (VxVec *)malloc(sizeof(VxVec));
    if (!c) return vx_make_fail();
    c->refs = 1;
    c->len = 0;
    c->cap = 0;
    c->items = NULL;
    VxValue v; v.kind = VXK_VEC; v.as.vec = c; return v;
}

void vx_retain(VxValue *v) {
    if (!v) return;
    if (v->kind == VXK_TEXT && v->as.text) v->as.text->refs++;
    else if (v->kind == VXK_VEC && v->as.vec) v->as.vec->refs++;
}

void vx_release(VxValue *v) {
    if (!v) return;
    if (v->kind == VXK_TEXT && v->as.text) {
        VxText *t = v->as.text;
        if (--t->refs <= 0) free(t);
        v->as.text = NULL;
    } else if (v->kind == VXK_VEC && v->as.vec) {
        VxVec *c = v->as.vec;
        if (--c->refs <= 0) {
            for (size_t i = 0; i < c->len; i++) vx_release(&c->items[i]);
            free(c->items);
            free(c);
        }
        v->as.vec = NULL;
    }
    v->kind = VXK_FAIL;
}

void vx_value_free(VxValue *v) {
    if (!v) return;
    VxValue tmp = *v;
    vx_release(&tmp);
}

bool vx_is_fail(VxValue v) { return v.kind == VXK_FAIL; }

bool vx_is_true(VxValue v) {
    switch (v.kind) {
        case VXK_FAIL: return false;
        case VXK_NUM: return v.as.num != 0.0 && !isnan(v.as.num);
        case VXK_TEXT: return v.as.text && v.as.text->len > 0;
        case VXK_VEC: return v.as.vec && v.as.vec->len > 0;
        default: return false;
    }
}

/* text view of a value for concat; caller frees */
static char *num_text(double n) {
    char buf[64];
    if (isnan(n)) { strcpy(buf, "fail"); }
    else if (isinf(n)) { snprintf(buf, sizeof(buf), n > 0 ? "inf" : "0 - inf"); }
    else {
        long li = (long)n;
        if ((double)li == n) snprintf(buf, sizeof(buf), "%ld", li);
        else snprintf(buf, sizeof(buf), "%.10g", n);
    }
    size_t L = strlen(buf);
    char *d = (char *)malloc(L + 1);
    if (d) memcpy(d, buf, L + 1);
    return d;
}

char *vx_repr(VxValue v) {
    switch (v.kind) {
        case VXK_FAIL: {
            char *d = (char *)malloc(5);
            if (d) memcpy(d, "fail", 5);
            return d;
        }
        case VXK_NUM: return num_text(v.as.num);
        case VXK_TEXT: {
            size_t L = v.as.text ? v.as.text->len : 0;
            char *d = (char *)malloc(L + 1);
            if (!d) return NULL;
            if (L) memcpy(d, v.as.text->data, L);
            d[L] = '\0';
            return d;
        }
        case VXK_VEC: {
            /* [a b c] with repr of items; texts raw, numbers formatted */
            size_t cap = 64, len = 0;
            char *d = (char *)malloc(cap);
            if (!d) return NULL;
            d[len++] = '[';
            if (v.as.vec) {
                for (size_t i = 0; i < v.as.vec->len; i++) {
                    char *ri = vx_repr(v.as.vec->items[i]);
                    if (!ri) continue;
                    size_t rl = strlen(ri);
                    if (i > 0) {
                        if (len + 1 >= cap) {
                            cap *= 2;
                            char *nd = (char *)realloc(d, cap);
                            if (!nd) { free(ri); break; }
                            d = nd;
                        }
                        d[len++] = ' ';
                    }
                    while (len + rl + 2 >= cap) {
                        cap *= 2;
                        char *nd = (char *)realloc(d, cap);
                        if (!nd) break;
                        d = nd;
                    }
                    memcpy(d + len, ri, rl);
                    len += rl;
                    free(ri);
                }
            }
            if (len + 2 >= cap) {
                char *nd = (char *)realloc(d, len + 2);
                if (!nd) { free(d); return NULL; }
                d = nd;
            }
            d[len++] = ']';
            d[len] = '\0';
            return d;
        }
        default: return NULL;
    }
}

VxValue vx_add(VxValue a, VxValue b) {
    if (a.kind == VXK_FAIL || b.kind == VXK_FAIL) return vx_make_fail();
    if (a.kind == VXK_NUM && b.kind == VXK_NUM) return vx_make_num(a.as.num + b.as.num);
    /* concat otherwise */
    char *sa = vx_repr(a);
    char *sb = vx_repr(b);
    if (!sa || !sb) { free(sa); free(sb); return vx_make_fail(); }
    size_t la = strlen(sa), lb = strlen(sb);
    VxText *t = (VxText *)malloc(sizeof(VxText) + la + lb + 1);
    if (!t) { free(sa); free(sb); return vx_make_fail(); }
    t->refs = 1;
    t->len = la + lb;
    memcpy(t->data, sa, la);
    memcpy(t->data + la, sb, lb);
    t->data[la + lb] = '\0';
    free(sa); free(sb);
    VxValue v; v.kind = VXK_TEXT; v.as.text = t; return v;
}

VxValue vx_sub(VxValue a, VxValue b) {
    if (a.kind != VXK_NUM || b.kind != VXK_NUM) return vx_make_fail();
    return vx_make_num(a.as.num - b.as.num);
}

VxValue vx_mul(VxValue a, VxValue b) {
    if (a.kind == VXK_FAIL || b.kind == VXK_FAIL) return vx_make_fail();
    if (a.kind == VXK_NUM && b.kind == VXK_NUM) return vx_make_num(a.as.num * b.as.num);
    /* text repeat */
    const VxValue *tv = NULL, *nv = NULL;
    if (a.kind == VXK_TEXT && b.kind == VXK_NUM) { tv = &a; nv = &b; }
    else if (a.kind == VXK_NUM && b.kind == VXK_TEXT) { tv = &b; nv = &a; }
    else return vx_make_fail();
    long n = (long)nv->as.num;
    if (n < 0) n = 0;
    if (n > 100000) n = 100000;
    size_t L = tv->as.text->len;
    VxText *t = (VxText *)malloc(sizeof(VxText) + L * (size_t)n + 1);
    if (!t) return vx_make_fail();
    t->refs = 1;
    t->len = L * (size_t)n;
    for (long i = 0; i < n; i++) memcpy(t->data + i * L, tv->as.text->data, L);
    t->data[t->len] = '\0';
    VxValue v; v.kind = VXK_TEXT; v.as.text = t; return v;
}

VxValue vx_div(VxValue a, VxValue b) {
    if (a.kind != VXK_NUM || b.kind != VXK_NUM) return vx_make_fail();
    if (b.as.num == 0.0) return vx_make_fail();
    return vx_make_num(a.as.num / b.as.num);
}

VxValue vx_mod(VxValue a, VxValue b) {
    if (a.kind != VXK_NUM || b.kind != VXK_NUM) return vx_make_fail();
    if (b.as.num == 0.0) return vx_make_fail();
    return vx_make_num(fmod(a.as.num, b.as.num));
}

VxValue vx_neg(VxValue a) {
    if (a.kind != VXK_NUM) return vx_make_fail();
    return vx_make_num(-a.as.num);
}

VxValue vx_cmp_gt(VxValue a, VxValue b) {
    if (a.kind != VXK_NUM || b.kind != VXK_NUM) return vx_make_fail();
    return vx_make_num(a.as.num > b.as.num ? 1 : 0);
}
VxValue vx_cmp_lt(VxValue a, VxValue b) {
    if (a.kind != VXK_NUM || b.kind != VXK_NUM) return vx_make_fail();
    return vx_make_num(a.as.num < b.as.num ? 1 : 0);
}
VxValue vx_cmp_gte(VxValue a, VxValue b) {
    if (a.kind != VXK_NUM || b.kind != VXK_NUM) return vx_make_fail();
    return vx_make_num(a.as.num >= b.as.num ? 1 : 0);
}
VxValue vx_cmp_lte(VxValue a, VxValue b) {
    if (a.kind != VXK_NUM || b.kind != VXK_NUM) return vx_make_fail();
    return vx_make_num(a.as.num <= b.as.num ? 1 : 0);
}

static bool val_same(VxValue a, VxValue b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case VXK_FAIL: return true;
        case VXK_NUM: return a.as.num == b.as.num;
        case VXK_TEXT: {
            if (a.as.text->len != b.as.text->len) return false;
            return memcmp(a.as.text->data, b.as.text->data, a.as.text->len) == 0;
        }
        case VXK_VEC: {
            if (a.as.vec->len != b.as.vec->len) return false;
            for (size_t i = 0; i < a.as.vec->len; i++) {
                if (!val_same(a.as.vec->items[i], b.as.vec->items[i])) return false;
            }
            return true;
        }
        default: return false;
    }
}

VxValue vx_cmp_eq(VxValue a, VxValue b) {
    return vx_make_num(val_same(a, b) ? 1 : 0);
}
VxValue vx_cmp_neq(VxValue a, VxValue b) {
    return vx_make_num(val_same(a, b) ? 0 : 1);
}

VxValue vx_logic_and(VxValue a, VxValue b) {
    bool r = vx_is_true(a) && vx_is_true(b);
    return vx_make_num(r ? 1 : 0);
}
VxValue vx_logic_or(VxValue a, VxValue b) {
    bool r = vx_is_true(a) || vx_is_true(b);
    return vx_make_num(r ? 1 : 0);
}
VxValue vx_logic_not(VxValue a) {
    return vx_make_num(vx_is_true(a) ? 0 : 1);
}

long vx_loop_count(VxValue v) {
    if (v.kind == VXK_FAIL) return 0;
    if (v.kind == VXK_NUM) {
        long n = (long)v.as.num;
        return n < 0 ? 0 : n;
    }
    return vx_is_true(v) ? 1 : 0;
}

/* helper used by VM to build vec from stack slice (takes ownership of items) */
bool vx_vec_from_items(VxValue *out, VxValue *items, size_t n) {
    VxVec *c = (VxVec *)malloc(sizeof(VxVec));
    if (!c) return false;
    c->refs = 1;
    c->len = n;
    c->cap = n;
    c->items = NULL;
    if (n) {
        c->items = (VxValue *)malloc(n * sizeof(VxValue));
        if (!c->items) { free(c); return false; }
        for (size_t i = 0; i < n; i++) c->items[i] = items[i];
    }
    out->kind = VXK_VEC;
    out->as.vec = c;
    return true;
}
