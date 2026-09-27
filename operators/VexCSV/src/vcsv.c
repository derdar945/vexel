/* VexCSV — small CSV for Vexel (pure C, no deps).
 * csv_parse # text -> vec of rows (vecs of text).
 * Quotes: "a ""quoted"" field"; \r stripped; \n separates rows.
 * Verbs: csv_parse/1, csv_rows/1, csv_cell/3.
 */
#include "operator.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *buf;
    size_t len, cap;
} SB;

static int sb_put(SB *b, char c) {
    if (b->len + 2 > b->cap) {
        size_t nc = (b->cap + 64) * 2;
        char *nb = (char *)realloc(b->buf, nc);
        if (!nb) return 1;
        b->buf = nb;
        b->cap = nc;
    }
    b->buf[b->len++] = c;
    return 0;
}

static int csv_push_text(const VxOpApi *api, VxOpVal *vec, const char *s,
                         size_t n) {
    VxOpVal *t = api->make_text(s ? s : "", s ? n : 0);
    if (!t) return 1;
    if (api->vec_push(vec, t) != 0) {
        api->release(t);
        return 1;
    }
    api->release(t);
    return 0;
}

/* returns vec-of-rows or NULL */
static VxOpVal *csv_parse_doc(const VxOpApi *api, const char *s, size_t n) {
    VxOpVal *rows = api->make_vec();
    VxOpVal *row = NULL;
    SB f;
    f.buf = NULL;
    f.len = 0;
    f.cap = 0;
    if (!rows) return NULL;
    row = api->make_vec();
    if (!row) {
        api->release(rows);
        return NULL;
    }
    f.buf = (char *)malloc(64);
    if (!f.buf) {
        api->release(row);
        api->release(rows);
        return NULL;
    }
    f.cap = 64;
    size_t p = 0;
    int in_q = 0;
    int field_used = 0; /* any char (or quotes) seen for current field */
    int row_used = 0;   /* any field pushed or content seen */
    int bad = 0;
    /* helper: flush field into row */
    while (p <= n) {
        char c = (p < n) ? s[p] : '\n'; /* sentinel newline flushes tail */
        if (in_q) {
            if (c == '"') {
                if (p + 1 < n && s[p + 1] == '"') {
                    if (sb_put(&f, '"')) {
                        bad = 1;
                        break;
                    }
                    p += 2;
                    field_used = 1;
                    row_used = 1;
                    continue;
                }
                in_q = 0;
                p++;
                continue;
            }
            if (p >= n) {
                bad = 1;
                break;
            }
            if (sb_put(&f, c)) {
                bad = 1;
                break;
            }
            p++;
            field_used = 1;
            row_used = 1;
            continue;
        }
        if (c == '"') {
            in_q = 1;
            field_used = 1;
            row_used = 1;
            p++;
            continue;
        }
        if (c == ',') {
            if (csv_push_text(api, row, f.buf, f.len)) {
                bad = 1;
                break;
            }
            f.len = 0;
            field_used = 0;
            row_used = 1;
            p++;
            continue;
        }
        if (c == '\r') {
            p++;
            continue;
        }
        if (c == '\n') {
            /* skip the very final sentinel if the whole doc was empty */
            if (p == n && !row_used && !field_used && f.len == 0) {
                p++;
                continue;
            }
            if (csv_push_text(api, row, f.buf, f.len)) {
                bad = 1;
                break;
            }
            f.len = 0;
            field_used = 0;
            if (api->vec_push(rows, row) != 0) {
                bad = 1;
                break;
            }
            api->release(row);
            row = api->make_vec();
            if (!row) {
                bad = 1;
                break;
            }
            row_used = 0;
            p++;
            continue;
        }
        if (sb_put(&f, c)) {
            bad = 1;
            break;
        }
        field_used = 1;
        row_used = 1;
        p++;
    }
    free(f.buf);
    if (row) api->release(row);
    if (bad || in_q) {
        api->release(rows);
        return NULL;
    }
    return rows;
}

/* csv_parse # text */
static int c_parse(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    VxOpVal *v = csv_parse_doc(api, s, n);
    if (!v) return 1;
    *out = v;
    return 0;
}

/* csv_rows # text — row count */
static int c_rows(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    VxOpVal *v = csv_parse_doc(api, s, n);
    if (!v) return 1;
    size_t m = 0;
    int ok = api->vec_len(v, &m);
    api->release(v);
    if (!ok) return 1;
    *out = api->make_num((double)m);
    return *out ? 0 : 1;
}

/* csv_cell # text, row, col — text or fail */
static int c_cell(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    double r = 0, c = 0;
    if (!api->as_text(a[0], &s, &n) || !api->as_num(a[1], &r) ||
        !api->as_num(a[2], &c))
        return 1;
    if (r < 0 || c < 0) return 1;
    VxOpVal *v = csv_parse_doc(api, s, n);
    if (!v) return 1;
    VxOpVal *row = api->vec_get(v, (size_t)r);
    api->release(v);
    if (!row) return 1;
    VxOpVal *cell = api->vec_get(row, (size_t)c);
    api->release(row);
    if (!cell) return 1;
    const char *b = NULL;
    size_t m = 0;
    if (!api->as_text(cell, &b, &m)) {
        api->release(cell);
        return 1;
    }
    VxOpVal *t = api->make_text(b, m);
    api->release(cell);
    if (!t) return 1;
    *out = t;
    return 0;
}

static const VxOpFuncInfo g_funcs[] = {
    { "csv_parse", 1, c_parse },
    { "csv_rows", 1, c_rows },
    { "csv_cell", 3, c_cell },
};

#ifdef _WIN32
#define VXOP_EXPORT __declspec(dllexport)
#else
#define VXOP_EXPORT
#endif

VXOP_EXPORT const VxOpInfo *vxop_open(const VxOpApi *api) {
    static VxOpInfo info;
    (void)api;
    info.abi_version = VXOP_ABI_VERSION;
    info.name = "VexCSV";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
