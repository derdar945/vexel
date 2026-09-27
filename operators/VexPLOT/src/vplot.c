/* VexPLOT — ASCII charts for Vexel (pure C, no deps).
 * plot_bar # vec, width — horizontal bars, one row per number.
 * plot_line # vec, width, height — vertical sparkline canvas.
 * Non-numbers fail the call. Output is plain text for > show.
 */
#include "operator.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int plot_nums(const VxOpApi *api, VxOpVal *v, double **out,
                     size_t *count) {
    size_t n = 0;
    if (api->face(v) != VXF_VEC || !api->vec_len(v, &n) || n == 0 ||
        n > 100000)
        return 0;
    double *xs = (double *)malloc(n * sizeof(double));
    if (!xs) return 0;
    for (size_t i = 0; i < n; i++) {
        VxOpVal *it = api->vec_get(v, i);
        if (!it) {
            free(xs);
            return 0;
        }
        double d = 0;
        int ok = api->as_num(it, &d);
        api->release(it);
        if (!ok) {
            free(xs);
            return 0;
        }
        xs[i] = d;
    }
    *out = xs;
    *count = n;
    return 1;
}

/* plot_bar # vec, width */
static int p_bar(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double w = 0;
    double *xs = NULL;
    size_t n = 0;
    if (!api->as_num(a[1], &w) || w < 4 || w > 200) return 1;
    if (!plot_nums(api, a[0], &xs, &n)) return 1;
    double mx = xs[0];
    for (size_t i = 1; i < n; i++)
        if (xs[i] > mx) mx = xs[i];
    if (mx <= 0) mx = 1;
    size_t cap = n * ((size_t)w + 16) + 1, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        free(xs);
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        int bars = (int)((xs[i] / mx) * w);
        if (xs[i] > 0 && bars == 0) bars = 1;
        if (bars < 0) bars = 0;
        if (len + (size_t)bars + 16 > cap) break;
        for (int b = 0; b < bars; b++) buf[len++] = '#';
        buf[len++] = ' ';
        len += (size_t)snprintf(buf + len, cap - len, "%g", xs[i]);
        buf[len++] = '\n';
    }
    free(xs);
    VxOpVal *t = api->make_text(buf, len);
    free(buf);
    if (!t) return 1;
    *out = t;
    return 0;
}

/* plot_line # vec, width, height */
static int p_line(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double w = 0, h = 0;
    double *xs = NULL;
    size_t n = 0;
    if (!api->as_num(a[1], &w) || !api->as_num(a[2], &h) || w < 2 ||
        w > 200 || h < 2 || h > 60)
        return 1;
    if (!plot_nums(api, a[0], &xs, &n)) return 1;
    int W = (int)w, H = (int)h;
    if ((size_t)W > n) W = (int)n;
    /* sample W columns across n values */
    double mn = xs[0], mx = xs[0];
    for (size_t i = 1; i < n; i++) {
        if (xs[i] < mn) mn = xs[i];
        if (xs[i] > mx) mx = xs[i];
    }
    if (mx == mn) mx = mn + 1;
    size_t cap = (size_t)(W + 1) * (size_t)H + 1, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        free(xs);
        return 1;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            size_t si = n == 1 ? 0 : (size_t)(((double)x / (W - 1)) * (n - 1));
            if (si >= n) si = n - 1;
            int row = (int)(((xs[si] - mn) / (mx - mn)) * (H - 1));
            buf[len++] = (H - 1 - y == row) ? '*' : ' ';
        }
        buf[len++] = '\n';
    }
    free(xs);
    VxOpVal *t = api->make_text(buf, len);
    free(buf);
    if (!t) return 1;
    *out = t;
    return 0;
}

static const VxOpFuncInfo g_funcs[] = {
    { "plot_bar", 2, p_bar },
    { "plot_line", 3, p_line },
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
    info.name = "VexPLOT";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
