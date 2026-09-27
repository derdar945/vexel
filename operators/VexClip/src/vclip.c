/* VexClip — Windows clipboard for Vexel (user32 only).
 * clip_get/0 — text or fail; clip_set/1 — 1 ok; clip_has/0 — 1/0.
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

static char *w_to_u8(const wchar_t *w) {
    int m = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (m <= 0) return NULL;
    char *s = (char *)malloc((size_t)m);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, m, NULL, NULL);
    return s;
}

/* clip_has/0 */
static int c_has(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a;
    (void)argc;
    int has = 0;
    if (OpenClipboard(NULL)) {
        has = (IsClipboardFormatAvailable(CF_UNICODETEXT) ||
               IsClipboardFormatAvailable(CF_TEXT))
                  ? 1
                  : 0;
        CloseClipboard();
    }
    *out = api->make_num(has ? 1 : 0);
    return *out ? 0 : 1;
}

/* clip_get/0 */
static int c_get(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a;
    (void)argc;
    if (!OpenClipboard(NULL)) return 1;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    VxOpVal *v = NULL;
    if (h) {
        const wchar_t *w = (const wchar_t *)GlobalLock(h);
        if (w) {
            char *u = w_to_u8(w);
            if (u) {
                v = api->make_text(u, strlen(u));
                free(u);
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    if (!v) return 1;
    *out = v;
    return 0;
}

/* clip_set/1 */
static int c_set(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    size_t cells = wcslen(w) + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, cells * sizeof(wchar_t));
    if (!h) {
        free(w);
        return 1;
    }
    wchar_t *dst = (wchar_t *)GlobalLock(h);
    if (!dst) {
        GlobalFree(h);
        free(w);
        return 1;
    }
    memcpy(dst, w, cells * sizeof(wchar_t));
    GlobalUnlock(h);
    free(w);
    if (!OpenClipboard(NULL)) {
        GlobalFree(h);
        return 1;
    }
    EmptyClipboard();
    int ok = (SetClipboardData(CF_UNICODETEXT, h) != NULL);
    CloseClipboard();
    if (!ok) {
        GlobalFree(h);
        return 1;
    }
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "clip_get", 0, c_get },
    { "clip_set", 1, c_set },
    { "clip_has", 0, c_has },
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
    info.name = "VexClip";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
