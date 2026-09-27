/* VexJSON — tiny JSON for Vexel (pure C, no deps).
 * Values: object -> vec of [key value] pairs, array -> vec,
 * string -> text, number -> num, true/false -> 1/0, null -> fail.
 * Verbs: json_valid/1, json_parse/1, json_get/2, json_len/1.
 * json_get walks a dotted path: "users.0.name" (arrays by index).
 */
#include "operator.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    const char *s;
    size_t n, p;
    const VxOpApi *api;
    int bad;
} JP;

static void jp_ws(JP *j) {
    while (j->p < j->n && (j->s[j->p] == ' ' || j->s[j->p] == '\t' ||
                           j->s[j->p] == '\n' || j->s[j->p] == '\r'))
        j->p++;
}

static VxOpVal *jp_val(JP *j);

static int jp_hexdig(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* append UTF-8 for one codepoint; returns 0 ok */
static int jp_put_utf8(char **buf, size_t *len, size_t *cap, unsigned cp) {
    char tmp[4];
    int m = 0;
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        m = 1;
    } else if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        m = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        m = 3;
    } else if (cp < 0x110000) {
        tmp[0] = (char)(0xF0 | (cp >> 18));
        tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[3] = (char)(0x80 | (cp & 0x3F));
        m = 4;
    } else {
        return 1;
    }
    if (*len + (size_t)m + 1 > *cap) {
        size_t nc = (*cap + (size_t)m + 32) * 2;
        char *nb = (char *)realloc(*buf, nc);
        if (!nb) return 1;
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, tmp, (size_t)m);
    *len += (size_t)m;
    return 0;
}

static int jp_pushc(char **buf, size_t *len, size_t *cap, char c) {
    if (*len + 2 > *cap) {
        size_t nc = (*cap + 32) * 2;
        char *nb = (char *)realloc(*buf, nc);
        if (!nb) return 1;
        *buf = nb;
        *cap = nc;
    }
    (*buf)[(*len)++] = c;
    return 0;
}

static VxOpVal *jp_string(JP *j) {
    /* s[p] == '"' */
    j->p++;
    size_t cap = 64, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        j->bad = 1;
        return NULL;
    }
    for (;;) {
        if (j->p >= j->n) {
            free(buf);
            j->bad = 1;
            return NULL;
        }
        char c = j->s[j->p++];
        if (c == '"') break;
        if (c == '\\') {
            if (j->p >= j->n) {
                free(buf);
                j->bad = 1;
                return NULL;
            }
            char e = j->s[j->p++];
            if (e == 'u') {
                unsigned cp = 0;
                for (int k = 0; k < 4; k++) {
                    if (j->p >= j->n) {
                        free(buf);
                        j->bad = 1;
                        return NULL;
                    }
                    int h = jp_hexdig((unsigned char)j->s[j->p++]);
                    if (h < 0) {
                        free(buf);
                        j->bad = 1;
                        return NULL;
                    }
                    cp = (cp << 4) | (unsigned)h;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF && j->p + 5 < j->n &&
                    j->s[j->p] == '\\' && j->s[j->p + 1] == 'u') {
                    unsigned lo = 0;
                    int ok = 1;
                    for (int k = 0; k < 4; k++) {
                        int h = jp_hexdig((unsigned char)j->s[j->p + 2 + k]);
                        if (h < 0) {
                            ok = 0;
                            break;
                        }
                        lo = (lo << 4) | (unsigned)h;
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        j->p += 6;
                    }
                }
                if (jp_put_utf8(&buf, &len, &cap, cp)) {
                    free(buf);
                    j->bad = 1;
                    return NULL;
                }
            } else {
                char m = e;
                if (e == 'n') m = '\n';
                else if (e == 't') m = '\t';
                else if (e == 'r') m = '\r';
                else if (e == 'b') m = '\b';
                else if (e == 'f') m = '\f';
                if (jp_pushc(&buf, &len, &cap, m)) {
                    free(buf);
                    j->bad = 1;
                    return NULL;
                }
            }
        } else {
            if (jp_pushc(&buf, &len, &cap, c)) {
                free(buf);
                j->bad = 1;
                return NULL;
            }
        }
    }
    VxOpVal *v = j->api->make_text(buf, len);
    free(buf);
    if (!v) j->bad = 1;
    return v;
}

static VxOpVal *jp_number(JP *j) {
    size_t start = j->p;
    if (j->p < j->n && (j->s[j->p] == '-' || j->s[j->p] == '+')) j->p++;
    while (j->p < j->n && j->s[j->p] >= '0' && j->s[j->p] <= '9') j->p++;
    if (j->p < j->n && j->s[j->p] == '.') {
        j->p++;
        while (j->p < j->n && j->s[j->p] >= '0' && j->s[j->p] <= '9') j->p++;
    }
    if (j->p < j->n && (j->s[j->p] == 'e' || j->s[j->p] == 'E')) {
        j->p++;
        if (j->p < j->n && (j->s[j->p] == '-' || j->s[j->p] == '+')) j->p++;
        while (j->p < j->n && j->s[j->p] >= '0' && j->s[j->p] <= '9') j->p++;
    }
    size_t L = j->p - start;
    if (L == 0 || L > 64) {
        j->bad = 1;
        return NULL;
    }
    char tmp[72];
    memcpy(tmp, j->s + start, L);
    tmp[L] = 0;
    VxOpVal *v = j->api->make_num(strtod(tmp, NULL));
    if (!v) j->bad = 1;
    return v;
}

static int jp_lit(JP *j, const char *w) {
    size_t L = strlen(w);
    if (j->p + L > j->n || memcmp(j->s + j->p, w, L) != 0) {
        j->bad = 1;
        return 0;
    }
    j->p += L;
    return 1;
}

/* push item into vec WITHOUT releasing (caller releases exactly once).
 * 0 ok, 1 oom. */
static int jp_push(const VxOpApi *api, VxOpVal *vec, VxOpVal *item) {
    if (!item) return 1;
    return api->vec_push(vec, item) != 0;
}

static VxOpVal *jp_array(JP *j) {
    j->p++; /* [ */
    VxOpVal *arr = j->api->make_vec();
    if (!arr) {
        j->bad = 1;
        return NULL;
    }
    jp_ws(j);
    if (j->p < j->n && j->s[j->p] == ']') {
        j->p++;
        return arr;
    }
    for (;;) {
        jp_ws(j);
        VxOpVal *v = jp_val(j);
        if (j->bad || !v) {
            if (v) j->api->release(v);
            j->api->release(arr);
            j->bad = 1;
            return NULL;
        }
        if (jp_push(j->api, arr, v)) {
            j->api->release(v);
            j->api->release(arr);
            j->bad = 1;
            return NULL;
        }
        j->api->release(v);
        jp_ws(j);
        if (j->p >= j->n) {
            j->api->release(arr);
            j->bad = 1;
            return NULL;
        }
        if (j->s[j->p] == ',') {
            j->p++;
            continue;
        }
        if (j->s[j->p] == ']') {
            j->p++;
            return arr;
        }
        j->api->release(arr);
        j->bad = 1;
        return NULL;
    }
}

static VxOpVal *jp_object(JP *j) {
    j->p++; /* { */
    VxOpVal *obj = j->api->make_vec();
    if (!obj) {
        j->bad = 1;
        return NULL;
    }
    jp_ws(j);
    if (j->p < j->n && j->s[j->p] == '}') {
        j->p++;
        return obj;
    }
    for (;;) {
        jp_ws(j);
        if (j->p >= j->n || j->s[j->p] != '"') {
            j->api->release(obj);
            j->bad = 1;
            return NULL;
        }
        VxOpVal *k = jp_string(j);
        if (j->bad || !k) {
            if (k) j->api->release(k);
            j->api->release(obj);
            j->bad = 1;
            return NULL;
        }
        jp_ws(j);
        if (j->p >= j->n || j->s[j->p] != ':') {
            j->api->release(k);
            j->api->release(obj);
            j->bad = 1;
            return NULL;
        }
        j->p++;
        jp_ws(j);
        VxOpVal *v = jp_val(j);
        if (j->bad || !v) {
            if (v) j->api->release(v);
            j->api->release(k);
            j->api->release(obj);
            j->bad = 1;
            return NULL;
        }
        VxOpVal *pair = j->api->make_vec();
        if (!pair || jp_push(j->api, pair, k) || jp_push(j->api, pair, v)) {
            if (pair) j->api->release(pair);
            j->api->release(k);
            j->api->release(v);
            j->api->release(obj);
            j->bad = 1;
            return NULL;
        }
        j->api->release(k);
        j->api->release(v);
        if (jp_push(j->api, obj, pair)) {
            j->api->release(pair);
            j->api->release(obj);
            j->bad = 1;
            return NULL;
        }
        j->api->release(pair);
        jp_ws(j);
        if (j->p >= j->n) {
            j->api->release(obj);
            j->bad = 1;
            return NULL;
        }
        if (j->s[j->p] == ',') {
            j->p++;
            continue;
        }
        if (j->s[j->p] == '}') {
            j->p++;
            return obj;
        }
        j->api->release(obj);
        j->bad = 1;
        return NULL;
    }
}

static VxOpVal *jp_val(JP *j) {
    jp_ws(j);
    if (j->p >= j->n) {
        j->bad = 1;
        return NULL;
    }
    char c = j->s[j->p];
    if (c == '{') return jp_object(j);
    if (c == '[') return jp_array(j);
    if (c == '"') return jp_string(j);
    if ((c >= '0' && c <= '9') || c == '-' || c == '+') return jp_number(j);
    if (c == 't') {
        if (!jp_lit(j, "true")) return NULL;
        VxOpVal *v = j->api->make_num(1);
        if (!v) j->bad = 1;
        return v;
    }
    if (c == 'f') {
        if (!jp_lit(j, "false")) return NULL;
        VxOpVal *v = j->api->make_num(0);
        if (!v) j->bad = 1;
        return v;
    }
    if (c == 'n') {
        if (!jp_lit(j, "null")) return NULL;
        VxOpVal *v = j->api->make_fail();
        if (!v) j->bad = 1;
        return v;
    }
    j->bad = 1;
    return NULL;
}

static VxOpVal *jp_parse_doc(const VxOpApi *api, const char *s, size_t n) {
    JP j;
    j.s = s;
    j.n = n;
    j.p = 0;
    j.api = api;
    j.bad = 0;
    /* skip UTF-8 BOM */
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        j.p = 3;
    VxOpVal *v = jp_val(&j);
    if (j.bad || !v) {
        if (v) api->release(v);
        return NULL;
    }
    jp_ws(&j);
    if (j.p != j.n) {
        api->release(v);
        return NULL;
    }
    return v;
}

/* find object member by key; returns retained copy or NULL */
static VxOpVal *jp_member(const VxOpApi *api, VxOpVal *obj, const char *key,
                          size_t klen) {
    size_t n = 0;
    if (!api->vec_len(obj, &n)) return NULL;
    for (size_t i = 0; i < n; i++) {
        VxOpVal *pair = api->vec_get(obj, i);
        if (!pair) continue;
        size_t pl = 0;
        VxOpVal *found = NULL;
        if (api->vec_len(pair, &pl) && pl == 2) {
            VxOpVal *k = api->vec_get(pair, 0);
            if (k) {
                const char *kb = NULL;
                size_t kl = 0;
                if (api->as_text(k, &kb, &kl) && kl == klen &&
                    memcmp(kb, key, klen) == 0) {
                    found = api->vec_get(pair, 1);
                }
                api->release(k);
            }
        }
        api->release(pair);
        if (found) return found;
    }
    return NULL;
}

static int jp_is_index(const char *s, size_t n, size_t *out) {
    if (n == 0 || n > 10) return 0;
    size_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (size_t)(s[i] - '0');
    }
    *out = v;
    return 1;
}

/* json_valid # text — 1/0, never fails the call */
static int j_valid(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    int ok = 0;
    if (api->as_text(a[0], &s, &n)) {
        VxOpVal *v = jp_parse_doc(api, s, n);
        ok = (v != NULL);
        if (v) api->release(v);
    }
    *out = api->make_num(ok ? 1 : 0);
    return *out ? 0 : 1;
}

/* json_parse # text — parsed value */
static int j_parse(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    VxOpVal *v = jp_parse_doc(api, s, n);
    if (!v) return 1;
    *out = v;
    return 0;
}

/* json_get # text, path — value or fail */
static int j_get(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL, *path = NULL;
    size_t n = 0, plen = 0;
    if (!api->as_text(a[0], &s, &n) || !api->as_text(a[1], &path, &plen))
        return 1;
    VxOpVal *cur = jp_parse_doc(api, s, n);
    if (!cur) return 1;
    if (plen == 0) {
        *out = cur;
        return 0;
    }
    size_t i = 0;
    while (i <= plen) {
        size_t e = i;
        while (e < plen && path[e] != '.') e++;
        size_t sl = e - i;
        VxOpVal *next = NULL;
        if (api->face(cur) == VXF_VEC && sl > 0) {
            next = jp_member(api, cur, path + i, sl);
            if (!next) {
                size_t idx = 0;
                if (jp_is_index(path + i, sl, &idx)) {
                    size_t L = 0;
                    if (api->vec_len(cur, &L) && idx < L)
                        next = api->vec_get(cur, idx);
                }
            }
        }
        api->release(cur);
        if (!next) return 1;
        cur = next;
        i = e + 1;
    }
    *out = cur;
    return 0;
}

/* json_len # text — len of array/object/string, else fail */
static int j_len(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    VxOpVal *v = jp_parse_doc(api, s, n);
    if (!v) return 1;
    double L = -1;
    if (api->face(v) == VXF_VEC) {
        size_t m = 0;
        if (api->vec_len(v, &m)) L = (double)m;
    } else if (api->face(v) == VXF_TEXT) {
        const char *b = NULL;
        size_t m = 0;
        if (api->as_text(v, &b, &m)) L = (double)m;
    }
    api->release(v);
    if (L < 0) return 1;
    *out = api->make_num(L);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "json_valid", 1, j_valid },
    { "json_parse", 1, j_parse },
    { "json_get", 2, j_get },
    { "json_len", 1, j_len },
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
    info.name = "VexJSON";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
