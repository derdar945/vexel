/* VexTBL — small tables for Vexel (pure C, no deps).
 * A table is a vec of row-vecs of texts (exactly what csv_parse #
 * returns). Row 0 is the header: it stays on top through sort/pick.
 * Verbs: tbl_cols/1, tbl_cell/3, tbl_col/2, tbl_sort/2, tbl_pick/3.
 */
#include "operator.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* fetch row r as vec (retained). NULL on any shape error. */
static VxOpVal *tbl_row(const VxOpApi *api, VxOpVal *t, size_t r) {
    if (api->face(t) != VXF_VEC) return NULL;
    VxOpVal *row = api->vec_get(t, r);
    if (!row) return NULL;
    if (api->face(row) != VXF_VEC) {
        api->release(row);
        return NULL;
    }
    return row;
}

static int tbl_text(const VxOpApi *api, VxOpVal *v, const char **s,
                    size_t *n) {
    return api->as_text(v, s, n);
}

/* is the whole column numeric? returns 1/0. nums has nrows-1 slots
 * (body only, header excluded): row r lands in nums[r-1]. */
static int tbl_colnum(const VxOpApi *api, VxOpVal *t, size_t c, size_t nrows,
                      double *nums) {
    for (size_t r = 1; r < nrows; r++) {
        VxOpVal *row = tbl_row(api, t, r);
        if (!row) return 0;
        size_t m = 0;
        int ok = api->vec_len(row, &m);
        VxOpVal *cell = ok && c < m ? api->vec_get(row, c) : NULL;
        api->release(row);
        if (!cell) {
            nums[r - 1] = 0;
            continue;
        }
        const char *s = NULL;
        size_t n = 0;
        double v = 0;
        if (!tbl_text(api, cell, &s, &n)) {
            /* numbers may ride as num too */
            if (!api->as_num(cell, &v)) {
                api->release(cell);
                return 0;
            }
            nums[r - 1] = v;
            api->release(cell);
            continue;
        }
        if (n == 0 || n > 64) {
            api->release(cell);
            return 0;
        }
        char tmp[72];
        memcpy(tmp, s, n);
        tmp[n] = 0;
        char *ep = NULL;
        v = strtod(tmp, &ep);
        api->release(cell);
        if (!ep || *ep != 0) return 0;
        nums[r - 1] = v;
    }
    return 1;
}

/* tbl_cols # table — max row width */
static int t_cols(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    size_t n = 0, best = 0;
    if (api->face(a[0]) != VXF_VEC || !api->vec_len(a[0], &n)) return 1;
    for (size_t r = 0; r < n; r++) {
        VxOpVal *row = tbl_row(api, a[0], r);
        if (!row) return 1;
        size_t m = 0;
        int ok = api->vec_len(row, &m);
        api->release(row);
        if (!ok) return 1;
        if (m > best) best = m;
    }
    *out = api->make_num((double)best);
    return *out ? 0 : 1;
}

/* tbl_cell # table, r, c — text or fail */
static int t_cell(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double r = 0, c = 0;
    if (!api->as_num(a[1], &r) || !api->as_num(a[2], &c) || r < 0 || c < 0)
        return 1;
    VxOpVal *row = tbl_row(api, a[0], (size_t)r);
    if (!row) return 1;
    VxOpVal *cell = api->vec_get(row, (size_t)c);
    api->release(row);
    if (!cell) return 1;
    const char *s = NULL;
    size_t n = 0;
    double v = 0;
    VxOpVal *t = NULL;
    if (tbl_text(api, cell, &s, &n)) {
        t = api->make_text(s, n);
    } else if (api->as_num(cell, &v)) {
        char nb[64];
        snprintf(nb, sizeof(nb), "%g", v);
        t = api->make_text(nb, strlen(nb));
    }
    api->release(cell);
    if (!t) return 1;
    *out = t;
    return 0;
}

/* tbl_col # table, c — vec of column cells (missing = "") */
static int t_col(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double c = 0;
    size_t n = 0;
    if (!api->as_num(a[1], &c) || c < 0) return 1;
    if (api->face(a[0]) != VXF_VEC || !api->vec_len(a[0], &n)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    for (size_t r = 0; r < n; r++) {
        VxOpVal *row = tbl_row(api, a[0], r);
        VxOpVal *cell = NULL;
        if (row) {
            cell = api->vec_get(row, (size_t)c);
            api->release(row);
        }
        VxOpVal *t = NULL;
        if (cell) {
            const char *s = NULL;
            size_t m = 0;
            double d = 0;
            if (tbl_text(api, cell, &s, &m)) t = api->make_text(s, m);
            else if (api->as_num(cell, &d)) {
                char nb[64];
                snprintf(nb, sizeof(nb), "%g", d);
                t = api->make_text(nb, strlen(nb));
            }
            api->release(cell);
        } else {
            t = api->make_text("", 0);
        }
        if (!t || api->vec_push(v, t) != 0) {
            if (t) api->release(t);
            api->release(v);
            return 1;
        }
        api->release(t);
    }
    *out = v;
    return 0;
}

/* compare key for sort: numeric or byte text */
typedef struct {
    double num;
    char *txt;
    size_t idx;
} SortKey;

static int cmp_num(const void *pa, const void *pb) {
    const SortKey *a = (const SortKey *)pa, *b = (const SortKey *)pb;
    if (a->num < b->num) return -1;
    if (a->num > b->num) return 1;
    return 0;
}

static int cmp_txt(const void *pa, const void *pb) {
    const SortKey *a = (const SortKey *)pa, *b = (const SortKey *)pb;
    size_t n = strlen(a->txt) < strlen(b->txt) ? strlen(a->txt)
                                               : strlen(b->txt);
    int r = memcmp(a->txt, b->txt, n);
    if (r) return r;
    if (strlen(a->txt) < strlen(b->txt)) return -1;
    if (strlen(a->txt) > strlen(b->txt)) return 1;
    return 0;
}

/* tbl_sort # table, col — header stays, body sorted asc */
static int t_sort(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double c = 0;
    size_t n = 0;
    if (!api->as_num(a[1], &c) || c < 0) return 1;
    if (api->face(a[0]) != VXF_VEC || !api->vec_len(a[0], &n) || n == 0)
        return 1;
    size_t body = n - 1;
    SortKey *ks = NULL;
    double *nums = NULL;
    VxOpVal *res = NULL;
    int rc = 1;
    if (body) {
        ks = (SortKey *)calloc(body ? body : 1, sizeof(SortKey));
        nums = (double *)calloc(body ? body : 1, sizeof(double));
        if (!ks || !nums) goto done;
    }
    int numeric = body ? tbl_colnum(api, a[0], (size_t)c, n, nums) : 1;
    for (size_t i = 0; i < body; i++) {
        ks[i].idx = i + 1;
        ks[i].num = nums[i];
        ks[i].txt = NULL;
        if (!numeric) {
            VxOpVal *row = tbl_row(api, a[0], i + 1);
            VxOpVal *cell = row ? api->vec_get(row, (size_t)c) : NULL;
            if (row) api->release(row);
            const char *s = "";
            size_t m = 0;
            if (cell) {
                const char *b = NULL;
                size_t bl = 0;
                if (tbl_text(api, cell, &b, &bl)) {
                    s = b;
                    m = bl;
                }
                api->release(cell);
            }
            ks[i].txt = (char *)malloc(m + 1);
            if (!ks[i].txt) goto done;
            memcpy(ks[i].txt, s, m);
            ks[i].txt[m] = 0;
        }
    }
    if (body) qsort(ks, body, sizeof(SortKey), numeric ? cmp_num : cmp_txt);
    res = api->make_vec();
    if (!res) goto done;
    for (size_t k = 0; k < n; k++) {
        size_t src = (k == 0 || !body) ? k : ks[k - 1].idx;
        VxOpVal *row = api->vec_get(a[0], src);
        if (!row || api->vec_push(res, row) != 0) {
            if (row) api->release(row);
            api->release(res);
            res = NULL;
            goto done;
        }
        api->release(row);
    }
    rc = 0;
done:
    if (ks) {
        for (size_t i = 0; i < body; i++) free(ks[i].txt);
        free(ks);
    }
    free(nums);
    if (rc || !res) return 1;
    *out = res;
    return 0;
}

/* tbl_pick # table, col, value — header + rows where cell == value */
static int t_pick(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double c = 0;
    const char *want = NULL;
    size_t wantn = 0;
    size_t n = 0;
    if (!api->as_num(a[1], &c) || c < 0) return 1;
    if (!api->as_text(a[2], &want, &wantn)) return 1;
    if (api->face(a[0]) != VXF_VEC || !api->vec_len(a[0], &n) || n == 0)
        return 1;
    VxOpVal *res = api->make_vec();
    if (!res) return 1;
    for (size_t r = 0; r < n; r++) {
        VxOpVal *row = tbl_row(api, a[0], r);
        if (!row) {
            api->release(res);
            return 1;
        }
        int keep = (r == 0);
        if (!keep) {
            VxOpVal *cell = api->vec_get(row, (size_t)c);
            if (cell) {
                const char *s = NULL;
                size_t m = 0;
                if (tbl_text(api, cell, &s, &m) && m == wantn &&
                    memcmp(s, want, m) == 0)
                    keep = 1;
                api->release(cell);
            }
        }
        if (keep) {
            if (api->vec_push(res, row) != 0) {
                api->release(row);
                api->release(res);
                return 1;
            }
        }
        api->release(row);
    }
    *out = res;
    return 0;
}

static const VxOpFuncInfo g_funcs[] = {
    { "tbl_cols", 1, t_cols },
    { "tbl_cell", 3, t_cell },
    { "tbl_col", 2, t_col },
    { "tbl_sort", 2, t_sort },
    { "tbl_pick", 3, t_pick },
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
    info.name = "VexTBL";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
