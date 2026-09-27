/* VexKV — tiny file-backed key-value store for Vexel (pure C).
 * File format: lines "key\tvalue\n", value escapes: \\ -> backslash,
 * \n stays "\n" as backslash+n, \t as backslash+t.
 * Keys must not contain \t \n \r or backslash (call fails).
 * Verbs: kv_set/3, kv_get/2, kv_del/2, kv_list/1, kv_has/2.
 */
#include "operator.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int kv_key_ok(const char *s, size_t n) {
    if (n == 0 || n > 4096) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\\')
            return 0;
    }
    return 1;
}

static char *kv_escape(const char *s, size_t n) {
    char *o = (char *)malloc(n * 2 + 1);
    size_t w = 0;
    if (!o) return NULL;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\') {
            o[w++] = '\\';
            o[w++] = '\\';
        } else if (s[i] == '\n') {
            o[w++] = '\\';
            o[w++] = 'n';
        } else if (s[i] == '\t') {
            o[w++] = '\\';
            o[w++] = 't';
        } else if (s[i] == '\r') {
            continue;
        } else {
            o[w++] = s[i];
        }
    }
    o[w] = 0;
    return o;
}

static char *kv_unescape(const char *s, size_t n, size_t *out_n) {
    char *o = (char *)malloc(n + 1);
    size_t w = 0;
    if (!o) return NULL;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            if (s[i + 1] == 'n') {
                o[w++] = '\n';
                i++;
            } else if (s[i + 1] == 't') {
                o[w++] = '\t';
                i++;
            } else if (s[i + 1] == '\\') {
                o[w++] = '\\';
                i++;
            } else {
                o[w++] = s[i];
            }
        } else {
            o[w++] = s[i];
        }
    }
    o[w] = 0;
    if (out_n) *out_n = w;
    return o;
}

typedef struct {
    char *k;
    size_t kn;
    char *v; /* raw escaped value as stored */
    size_t vn;
} KVRow;

static void kv_free_rows(KVRow *rows, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(rows[i].k);
        free(rows[i].v);
    }
    free(rows);
}

/* load all rows; missing file = empty set (not an error). */
static int kv_load(const char *path, KVRow **rows, size_t *count) {
    *rows = NULL;
    *count = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 8 * 1024 * 1024) {
        fclose(f);
        return 1;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return 1;
    }
    size_t got = sz ? fread(buf, 1, (size_t)sz, f) : 0;
    fclose(f);
    size_t cap = 16, n = 0;
    KVRow *r = (KVRow *)malloc(cap * sizeof(KVRow));
    if (!r) {
        free(buf);
        return 1;
    }
    size_t line = 0;
    while (line < got) {
        size_t e = line;
        while (e < got && buf[e] != '\n') e++;
        size_t el = e - line;
        if (el && buf[line + el - 1] == '\r') el--;
        if (el) {
            char *tab = memchr(buf + line, '\t', el);
            if (tab) {
                size_t kl = (size_t)(tab - (buf + line));
                size_t vl = el - kl - 1;
                if (n == cap) {
                    cap *= 2;
                    KVRow *nr = (KVRow *)realloc(r, cap * sizeof(KVRow));
                    if (!nr) {
                        kv_free_rows(r, n);
                        free(buf);
                        return 1;
                    }
                    r = nr;
                }
                r[n].k = (char *)malloc(kl + 1);
                r[n].v = (char *)malloc(vl + 1);
                if (!r[n].k || !r[n].v) {
                    free(r[n].k);
                    free(r[n].v);
                    kv_free_rows(r, n);
                    free(buf);
                    return 1;
                }
                memcpy(r[n].k, buf + line, kl);
                r[n].k[kl] = 0;
                r[n].kn = kl;
                memcpy(r[n].v, tab + 1, vl);
                r[n].v[vl] = 0;
                r[n].vn = vl;
                n++;
            }
        }
        line = e + 1;
    }
    free(buf);
    *rows = r;
    *count = n;
    return 0;
}

static int kv_save(const char *path, KVRow *rows, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    for (size_t i = 0; i < n; i++) {
        if (fwrite(rows[i].k, 1, rows[i].kn, f) != rows[i].kn ||
            fputc('\t', f) == EOF ||
            fwrite(rows[i].v, 1, rows[i].vn, f) != rows[i].vn ||
            fputc('\n', f) == EOF) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int kv_path(const VxOpApi *api, VxOpVal *v, char *out, size_t cap) {
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(v, &s, &n) || n == 0 || n >= cap) return 0;
    memcpy(out, s, n);
    out[n] = 0;
    return 1;
}

/* kv_set # file, key, value — 1 ok */
static int k_set(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    char path[1024];
    const char *k = NULL, *v = NULL;
    size_t kn = 0, vn = 0;
    if (!kv_path(api, a[0], path, sizeof(path)) ||
        !api->as_text(a[1], &k, &kn) || !api->as_text(a[2], &v, &vn) ||
        !kv_key_ok(k, kn))
        return 1;
    char *esc = kv_escape(v, vn);
    if (!esc) return 1;
    KVRow *rows = NULL;
    size_t n = 0;
    if (kv_load(path, &rows, &n)) {
        free(esc);
        return 1;
    }
    size_t at = n;
    for (size_t i = 0; i < n; i++) {
        if (rows[i].kn == kn && memcmp(rows[i].k, k, kn) == 0) {
            at = i;
            break;
        }
    }
    size_t total = n;
    if (at < n) {
        free(rows[at].v);
        rows[at].v = esc;
        rows[at].vn = strlen(esc);
    } else {
        KVRow *nr = (KVRow *)realloc(rows, (n + 1) * sizeof(KVRow));
        if (!nr) {
            kv_free_rows(rows, n);
            free(esc);
            return 1;
        }
        rows = nr;
        rows[n].k = (char *)malloc(kn + 1);
        if (!rows[n].k) {
            kv_free_rows(rows, n);
            free(esc);
            return 1;
        }
        memcpy(rows[n].k, k, kn);
        rows[n].k[kn] = 0;
        rows[n].kn = kn;
        rows[n].v = esc;
        rows[n].vn = strlen(esc);
        total = n + 1;
    }
    /* esc is owned by rows from here */
    int rc = kv_save(path, rows, total);
    kv_free_rows(rows, total);
    if (rc) return 1;
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* kv_get # file, key — value or fail */
static int k_get(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    char path[1024];
    const char *k = NULL;
    size_t kn = 0;
    if (!kv_path(api, a[0], path, sizeof(path)) ||
        !api->as_text(a[1], &k, &kn))
        return 1;
    KVRow *rows = NULL;
    size_t n = 0;
    if (kv_load(path, &rows, &n)) return 1;
    const char *found = NULL;
    size_t found_n = 0;
    for (size_t i = 0; i < n; i++) {
        if (rows[i].kn == kn && memcmp(rows[i].k, k, kn) == 0) {
            found = rows[i].v;
            found_n = rows[i].vn;
        }
    }
    VxOpVal *v = NULL;
    if (found) {
        size_t un = 0;
        char *u = kv_unescape(found, found_n, &un);
        if (u) v = api->make_text(u, un);
        free(u);
    }
    kv_free_rows(rows, n);
    if (!v) return 1;
    *out = v;
    return 0;
}

/* kv_del # file, key — 1 removed, fail if missing */
static int k_del(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    char path[1024];
    const char *k = NULL;
    size_t kn = 0;
    if (!kv_path(api, a[0], path, sizeof(path)) ||
        !api->as_text(a[1], &k, &kn))
        return 1;
    KVRow *rows = NULL;
    size_t n = 0;
    if (kv_load(path, &rows, &n)) return 1;
    size_t at = n;
    for (size_t i = 0; i < n; i++) {
        if (rows[i].kn == kn && memcmp(rows[i].k, k, kn) == 0) at = i;
    }
    if (at == n) {
        kv_free_rows(rows, n);
        return 1;
    }
    free(rows[at].k);
    free(rows[at].v);
    for (size_t i = at + 1; i < n; i++) rows[i - 1] = rows[i];
    int rc = kv_save(path, rows, n - 1);
    free(rows);
    if (rc) return 1;
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* kv_list # file — vec of keys (missing file = empty vec) */
static int k_list(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    char path[1024];
    if (!kv_path(api, a[0], path, sizeof(path))) return 1;
    KVRow *rows = NULL;
    size_t n = 0;
    if (kv_load(path, &rows, &n)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) {
        kv_free_rows(rows, n);
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        VxOpVal *t = api->make_text(rows[i].k, rows[i].kn);
        if (!t || api->vec_push(v, t) != 0) {
            if (t) api->release(t);
            api->release(v);
            kv_free_rows(rows, n);
            return 1;
        }
        api->release(t);
    }
    kv_free_rows(rows, n);
    *out = v;
    return 0;
}

/* kv_has # file, key — 1/0 */
static int k_has(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    char path[1024];
    const char *k = NULL;
    size_t kn = 0;
    int has = 0;
    if (!kv_path(api, a[0], path, sizeof(path)) ||
        !api->as_text(a[1], &k, &kn))
        return 1;
    KVRow *rows = NULL;
    size_t n = 0;
    if (kv_load(path, &rows, &n)) return 1;
    for (size_t i = 0; i < n; i++) {
        if (rows[i].kn == kn && memcmp(rows[i].k, k, kn) == 0) has = 1;
    }
    kv_free_rows(rows, n);
    *out = api->make_num(has ? 1 : 0);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "kv_set", 3, k_set },
    { "kv_get", 2, k_get },
    { "kv_del", 2, k_del },
    { "kv_list", 1, k_list },
    { "kv_has", 2, k_has },
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
    info.name = "VexKV";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
