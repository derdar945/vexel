/* VexGUI — native Vexel operator. Win32, no frameworks.
 *
 * Bench view:
 *   @ VexGUI
 *   @ w : window # "Hi", 400, 300
 *   @ b : button # w, "OK", 20, 60
 *   > show # w
 *   * open # w :
 *     ? clicked # b :
 *       > "pressed"
 *     .
 *   .
 */
#include "operator.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum { O_WIN = 0, O_CTL = 1 } ObjKind;
typedef enum { C_TEXT = 0, C_BUTTON = 1, C_INPUT = 2, C_EDITOR = 3, C_LIST = 4 } CtlKind;

typedef struct Obj {
    int alive;
    int kind;    /* ObjKind */
    int ctlkind; /* CtlKind, for controls */
    HWND hwnd;
    int clicked;
    int changed;
} Obj;

static Obj *g_objs = NULL;
static int g_nobjs = 0, g_cap = 0;
static int g_class_ok = 0;

static int new_obj(void) {
    /* reuse dead slots first (handles stay small) */
    for (int i = 0; i < g_nobjs; i++) {
        if (!g_objs[i].alive) {
            memset(&g_objs[i], 0, sizeof(Obj));
            return i;
        }
    }
    if (g_nobjs + 1 > g_cap) {
        int nc = g_cap ? g_cap * 2 : 16;
        Obj *nd = (Obj *)realloc(g_objs, (size_t)nc * sizeof(Obj));
        if (!nd) return -1;
        g_objs = nd;
        g_cap = nc;
    }
    memset(&g_objs[g_nobjs], 0, sizeof(Obj));
    return g_nobjs++;
}

/* handle = index + 1 (0 never valid) */
static int obj_of(VxOpVal *h, const VxOpApi *api) {
    double d = 0;
    if (!api->as_num(h, &d)) return -1;
    long i = (long)d;
    if (i < 1 || i > g_nobjs) return -1;
    if (!g_objs[i - 1].alive) return -1;
    return (int)(i - 1);
}

static void pump(void) {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static int find_by_hwnd(HWND hwnd) {
    for (int i = 0; i < g_nobjs; i++) {
        if (g_objs[i].alive && g_objs[i].hwnd == hwnd) return i;
    }
    return -1;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        int ci = id - 100;
        if (ci >= 0 && ci < g_nobjs && g_objs[ci].alive &&
            g_objs[ci].kind == O_CTL) {
            if (code == BN_CLICKED && g_objs[ci].ctlkind == C_BUTTON)
                g_objs[ci].clicked = 1;
            if (code == EN_CHANGE && g_objs[ci].ctlkind == C_INPUT)
                g_objs[ci].changed = 1;
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        int wi = find_by_hwnd(hwnd);
        if (wi >= 0) {
            g_objs[wi].alive = 0;
            /* children die with the window */
            for (int i = 0; i < g_nobjs; i++) {
                if (g_objs[i].alive && g_objs[i].kind == O_CTL &&
                    GetParent(g_objs[i].hwnd) == hwnd)
                    g_objs[i].alive = 0;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int ensure_class(void) {
    if (g_class_ok) return 1;
    WNDCLASSW kc;
    memset(&kc, 0, sizeof(kc));
    kc.lpfnWndProc = wnd_proc;
    kc.hInstance = GetModuleHandleW(NULL);
    kc.lpszClassName = L"VexGUIWindow";
    kc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    kc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW(&kc)) return 0;
    g_class_ok = 1;
    return 1;
}

/* UTF-8 <-> UTF-16 helpers */
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

static int api_text(const VxOpApi *api, VxOpVal *v, const char **s,
                    size_t *len) {
    if (!api->as_text(v, s, len)) return 0;
    return 1;
}

static int api_num(const VxOpApi *api, VxOpVal *v, double *d) {
    return api->as_num(v, d);
}

static VxOpVal *ok_num(const VxOpApi *api, double d) {
    return api->make_num(d);
}

/* ---------------- verbs ---------------- */

static int f_version(const VxOpApi *api, VxOpVal **a, int argc,
                     VxOpVal **out) {
    (void)a;
    (void)argc;
    *out = api->make_text("1.0.0", 5);
    return *out ? 0 : 1;
}

static int f_window(const VxOpApi *api, VxOpVal **a, int argc,
                    VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double w = 0, h = 0;
    if (!api_text(api, a[0], &t, &tl) || !api_num(api, a[1], &w) ||
        !api_num(api, a[2], &h))
        return 1;
    if (!ensure_class()) return 1;
    wchar_t *wt = u8_to_w(t, tl);
    if (!wt) return 1;
    int wi = (int)w, hi = (int)h;
    if (wi < 120) wi = 120;
    if (hi < 80) hi = 80;
    if (wi > 4096) wi = 4096;
    if (hi > 4096) hi = 4096;
    HWND hwnd = CreateWindowExW(0, L"VexGUIWindow", wt,
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, wi, hi, NULL,
                                NULL, GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!hwnd) return 1;
    int idx = new_obj();
    if (idx < 0) {
        DestroyWindow(hwnd);
        return 1;
    }
    g_objs[idx].alive = 1;
    g_objs[idx].kind = O_WIN;
    g_objs[idx].hwnd = hwnd;
    *out = ok_num(api, (double)(idx + 1));
    return *out ? 0 : 1;
}

static int need_win(const VxOpApi *api, VxOpVal *h, int *idx) {
    int i = obj_of(h, api);
    if (i < 0 || g_objs[i].kind != O_WIN) return 0;
    *idx = i;
    return 1;
}

static int need_ctl(const VxOpApi *api, VxOpVal *h, int ctlkind, int *idx) {
    int i = obj_of(h, api);
    if (i < 0 || g_objs[i].kind != O_CTL) return 0;
    if (ctlkind >= 0 && g_objs[i].ctlkind != ctlkind) return 0;
    *idx = i;
    return 1;
}

static int f_show(const VxOpApi *api, VxOpVal **a, int argc,
                  VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_win(api, a[0], &i)) return 1;
    ShowWindow(g_objs[i].hwnd, SW_SHOW);
    UpdateWindow(g_objs[i].hwnd);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int f_open(const VxOpApi *api, VxOpVal **a, int argc,
                  VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_win(api, a[0], &i)) {
        /* dead window reads as closed, not as crash */
        *out = ok_num(api, 0);
        return *out ? 0 : 1;
    }
    pump();
    *out = ok_num(api, g_objs[i].alive ? 1 : 0);
    return *out ? 0 : 1;
}

static int f_close(const VxOpApi *api, VxOpVal **a, int argc,
                   VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_win(api, a[0], &i)) return 1;
    g_objs[i].alive = 0;
    DestroyWindow(g_objs[i].hwnd);
    pump();
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int make_ctl(const VxOpApi *api, VxOpVal *win, const wchar_t *cls,
                    DWORD style, const char *txt, size_t txtlen, double x,
                    double y, double dw, double dh, int ctlkind,
                    VxOpVal **out) {
    int wi = 0;
    if (!need_win(api, win, &wi)) return 1;
    wchar_t *wt = u8_to_w(txt, txtlen);
    if (!wt) return 1;
    int idx = new_obj();
    if (idx < 0) {
        free(wt);
        return 1;
    }
    HWND hwnd = CreateWindowExW(0, cls, wt,
                                WS_CHILD | WS_VISIBLE | style,
                                (int)x, (int)y, (int)dw, (int)dh,
                                g_objs[wi].hwnd, (HMENU)(INT_PTR)(100 + idx),
                                GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!hwnd) {
        g_objs[idx].alive = 0;
        return 1;
    }
    g_objs[idx].alive = 1;
    g_objs[idx].kind = O_CTL;
    g_objs[idx].ctlkind = ctlkind;
    g_objs[idx].hwnd = hwnd;
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (f) SendMessageW(hwnd, WM_SETFONT, (WPARAM)f, 1);
    *out = ok_num(api, (double)(idx + 1));
    return *out ? 0 : 1;
}

static int f_text(const VxOpApi *api, VxOpVal **a, int argc,
                  VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double x = 0, y = 0;
    if (!api_text(api, a[1], &t, &tl) || !api_num(api, a[2], &x) ||
        !api_num(api, a[3], &y))
        return 1;
    return make_ctl(api, a[0], L"STATIC", SS_LEFT, t, tl, x, y, 220, 24,
                    C_TEXT, out);
}

static int f_button(const VxOpApi *api, VxOpVal **a, int argc,
                    VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double x = 0, y = 0;
    if (!api_text(api, a[1], &t, &tl) || !api_num(api, a[2], &x) ||
        !api_num(api, a[3], &y))
        return 1;
    return make_ctl(api, a[0], L"BUTTON", BS_PUSHBUTTON, t, tl, x, y, 120,
                    32, C_BUTTON, out);
}

static int f_input(const VxOpApi *api, VxOpVal **a, int argc,
                   VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0;
    if (!api_num(api, a[1], &x) || !api_num(api, a[2], &y)) return 1;
    return make_ctl(api, a[0], L"EDIT",
                    WS_BORDER | ES_AUTOHSCROLL | ES_LEFT, "", 0, x, y, 220,
                    26, C_INPUT, out);
}

/* editor # w, x, y, ww, hh — multiline EDIT with scroll */
static int f_editor(const VxOpApi *api, VxOpVal **a, int argc,
                    VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0, ww = 0, hh = 0;
    if (!api_num(api, a[1], &x) || !api_num(api, a[2], &y) ||
        !api_num(api, a[3], &ww) || !api_num(api, a[4], &hh))
        return 1;
    if (ww < 40) ww = 40;
    if (hh < 40) hh = 40;
    return make_ctl(api, a[0], L"EDIT",
                    WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                        ES_WANTRETURN | ES_LEFT,
                    "", 0, x, y, ww, hh, C_EDITOR, out);
}

/* list # w, x, y, ww, hh — LISTBOX with notify */
static int f_list(const VxOpApi *api, VxOpVal **a, int argc,
                  VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0, ww = 0, hh = 0;
    if (!api_num(api, a[1], &x) || !api_num(api, a[2], &y) ||
        !api_num(api, a[3], &ww) || !api_num(api, a[4], &hh))
        return 1;
    if (ww < 40) ww = 40;
    if (hh < 40) hh = 40;
    return make_ctl(api, a[0], L"LISTBOX",
                    WS_BORDER | WS_VSCROLL | LBS_NOTIFY, "", 0, x, y, ww,
                    hh, C_LIST, out);
}

/* list_set # h, vec — refill the listbox from a vec of texts */
static int f_list_set(const VxOpApi *api, VxOpVal **a, int argc,
                      VxOpVal **out) {
    (void)argc;
    int i = 0;
    size_t n = 0;
    if (!need_ctl(api, a[0], C_LIST, &i)) return 1;
    if (!api->vec_len(a[1], &n)) return 1;
    SendMessageW(g_objs[i].hwnd, LB_RESETCONTENT, 0, 0);
    for (size_t k = 0; k < n; k++) {
        VxOpVal *item = api->vec_get(a[1], k);
        if (!item) {
            SendMessageW(g_objs[i].hwnd, LB_RESETCONTENT, 0, 0);
            return 1;
        }
        const char *s = NULL;
        size_t sl = 0;
        double d = 0;
        wchar_t *w = NULL;
        if (api->as_text(item, &s, &sl)) {
            w = u8_to_w(s, sl);
        } else if (api->as_num(item, &d)) {
            char nb[64];
            snprintf(nb, sizeof(nb), "%g", d);
            w = u8_to_w(nb, strlen(nb));
        }
        api->release(item);
        if (!w) {
            SendMessageW(g_objs[i].hwnd, LB_RESETCONTENT, 0, 0);
            return 1;
        }
        SendMessageW(g_objs[i].hwnd, LB_ADDSTRING, 0, (LPARAM)w);
        free(w);
    }
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* selected # h — text of the chosen row, fail if none */
static int f_selected(const VxOpApi *api, VxOpVal **a, int argc,
                      VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_ctl(api, a[0], C_LIST, &i)) return 1;
    LRESULT sel = SendMessageW(g_objs[i].hwnd, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return 1;
    LRESULT L = SendMessageW(g_objs[i].hwnd, LB_GETTEXTLEN, (WPARAM)sel, 0);
    if (L == LB_ERR || L < 0 || L > 100000) return 1;
    wchar_t *w = (wchar_t *)malloc(((size_t)L + 1) * sizeof(wchar_t));
    if (!w) return 1;
    SendMessageW(g_objs[i].hwnd, LB_GETTEXT, (WPARAM)sel, (LPARAM)w);
    w[L] = 0;
    int m = WideCharToMultiByte(CP_UTF8, 0, w, (int)L, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)(m > 0 ? m : 1));
    if (!s) {
        free(w);
        return 1;
    }
    if (m > 0) WideCharToMultiByte(CP_UTF8, 0, w, (int)L, s, m, NULL, NULL);
    free(w);
    *out = api->make_text(s, (size_t)(m > 0 ? m : 0));
    free(s);
    return *out ? 0 : 1;
}

static int f_set_text(const VxOpApi *api, VxOpVal **a, int argc,
                      VxOpVal **out) {
    (void)argc;
    int i = 0;
    const char *t = NULL;
    size_t tl = 0;
    if (!need_ctl(api, a[0], -1, &i)) return 1;
    if (!api_text(api, a[1], &t, &tl)) return 1;
    wchar_t *wt = u8_to_w(t, tl);
    if (!wt) return 1;
    BOOL ok = SetWindowTextW(g_objs[i].hwnd, wt);
    free(wt);
    if (!ok) return 1;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int f_get_text(const VxOpApi *api, VxOpVal **a, int argc,
                      VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_ctl(api, a[0], -1, &i)) return 1;
    int n = GetWindowTextLengthW(g_objs[i].hwnd);
    if (n < 0) return 1;
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
    if (!w) return 1;
    GetWindowTextW(g_objs[i].hwnd, w, n + 1);
    int m = WideCharToMultiByte(CP_UTF8, 0, w, n, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)(m ? m : 1));
    if (!s) {
        free(w);
        return 1;
    }
    if (m) WideCharToMultiByte(CP_UTF8, 0, w, n, s, m, NULL, NULL);
    free(w);
    *out = api->make_text(s, (size_t)(m > 0 ? m : 0));
    free(s);
    return *out ? 0 : 1;
}

static int f_clicked(const VxOpApi *api, VxOpVal **a, int argc,
                     VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_ctl(api, a[0], C_BUTTON, &i)) return 1;
    pump();
    int hit = g_objs[i].alive && g_objs[i].clicked;
    g_objs[i].clicked = 0;
    *out = ok_num(api, hit ? 1 : 0);
    return *out ? 0 : 1;
}

static int f_changed(const VxOpApi *api, VxOpVal **a, int argc,
                     VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_ctl(api, a[0], C_INPUT, &i)) return 1;
    pump();
    int hit = g_objs[i].alive && g_objs[i].changed;
    g_objs[i].changed = 0;
    *out = ok_num(api, hit ? 1 : 0);
    return *out ? 0 : 1;
}

static int f_size(const VxOpApi *api, VxOpVal **a, int argc,
                  VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_win(api, a[0], &i)) return 1;
    RECT r;
    if (!GetClientRect(g_objs[i].hwnd, &r)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    VxOpVal *w = api->make_num((double)(r.right - r.left));
    VxOpVal *h = api->make_num((double)(r.bottom - r.top));
    if (!w || !h || api->vec_push(v, w) != 0 ||
        api->vec_push(v, h) != 0) {
        if (w) api->release(w);
        if (h) api->release(h);
        api->release(v);
        return 1;
    }
    api->release(w);
    api->release(h);
    *out = v;
    return 0;
}

/* run # w, "step" — own the message loop; summon the step tool each
 * round. The step answers wet to keep going, dry to land. */
static int f_run(const VxOpApi *api, VxOpVal **a, int argc,
                 VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_win(api, a[0], &i)) return 1;
    const char *s = NULL;
    size_t sl = 0;
    if (!api->as_text(a[1], &s, &sl) || sl == 0 || sl > 48) return 1;
    char step[64];
    memcpy(step, s, sl);
    step[sl] = '\0';
    if (!api->summon) return 1;
    for (;;) {
        pump();
        if (!g_objs[i].alive || !IsWindow(g_objs[i].hwnd)) break;
        VxOpVal *r = NULL;
        if (api->summon(step, NULL, 0, &r) != 0) {
            if (r) api->release(r);
            return 1;
        }
        int keep = api->is_true(r);
        if (r) api->release(r);
        if (!keep) break;
        Sleep(8);
    }
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "version", 0, f_version },
    { "window", 3, f_window },
    { "show", 1, f_show },
    { "open", 1, f_open },
    { "close", 1, f_close },
    { "run", 2, f_run },
    { "text", 4, f_text },
    { "button", 4, f_button },
    { "input", 3, f_input },
    { "editor", 5, f_editor },
    { "list", 5, f_list },
    { "list_set", 2, f_list_set },
    { "selected", 1, f_selected },
    { "set_text", 2, f_set_text },
    { "get_text", 1, f_get_text },
    { "clicked", 1, f_clicked },
    { "changed", 1, f_changed },
    { "size", 1, f_size },
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
    info.name = "VexGUI";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
