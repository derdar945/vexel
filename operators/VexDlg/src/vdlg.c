/* VexDlg — native Win32 dialogs for Vexel (user32 only).
 * dlg_msg # text, title — info box, always 1.
 * dlg_ok  # text, title — OK/Cancel box, 1/0.
 * dlg_ask # text, title — Yes/No/Cancel: 1/0/fail.
 */
#include "operator.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdlib.h>
#include <string.h>

static wchar_t *u8_to_w(const char *s, size_t len) {
    if (len == 0) {
        wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t));
        if (w) w[0] = 0;
        return w;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, (int)len, w, n);
    w[n] = 0;
    return w;
}

static int dlg_texts(const VxOpApi *api, VxOpVal **a, wchar_t **wt,
                     wchar_t **wc) {
    const char *t = NULL, *c = NULL;
    size_t tn = 0, cn = 0;
    *wt = NULL;
    *wc = NULL;
    if (!api->as_text(a[0], &t, &tn) || !api->as_text(a[1], &c, &cn))
        return 0;
    *wt = u8_to_w(t, tn);
    *wc = u8_to_w(c, cn);
    if (!*wt || !*wc) {
        free(*wt);
        free(*wc);
        *wt = *wc = NULL;
        return 0;
    }
    return 1;
}

static int d_msg(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    wchar_t *t = NULL, *c = NULL;
    if (!dlg_texts(api, a, &t, &c)) return 1;
    MessageBoxW(NULL, t, c, MB_OK | MB_ICONINFORMATION);
    free(t);
    free(c);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

static int d_ok(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    wchar_t *t = NULL, *c = NULL;
    if (!dlg_texts(api, a, &t, &c)) return 1;
    int r = MessageBoxW(NULL, t, c, MB_OKCANCEL | MB_ICONQUESTION);
    free(t);
    free(c);
    *out = api->make_num(r == IDOK ? 1 : 0);
    return *out ? 0 : 1;
}

static int d_ask(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    wchar_t *t = NULL, *c = NULL;
    if (!dlg_texts(api, a, &t, &c)) return 1;
    int r = MessageBoxW(NULL, t, c, MB_YESNOCANCEL | MB_ICONQUESTION);
    free(t);
    free(c);
    if (r == IDYES) {
        *out = api->make_num(1);
        return *out ? 0 : 1;
    }
    if (r == IDNO) {
        *out = api->make_num(0);
        return *out ? 0 : 1;
    }
    return 1; /* cancel/close -> fail */
}

static const VxOpFuncInfo g_funcs[] = {
    { "dlg_msg", 2, d_msg },
    { "dlg_ok", 2, d_ok },
    { "dlg_ask", 2, d_ask },
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
    info.name = "VexDlg";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
