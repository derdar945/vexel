/* VexUI — beautiful dark UI operator for Vexel (Win32 user32/gdi32 only).
 *
 * Bench view:
 *   @ VexUI
 *   @ w : ui_window # "Dark", 480, 360
 *   @ c : ui_card # w, 20, 70, 440, 120, 2238550
 *   @ t : ui_title # w, "Hello", 40, 90, 26
 *   @ b : ui_btn # w, "Go", 40, 200, 140, 40, 8158335
 *   > ui_show # w
 *   > ui_run # w, "step"
 *
 * Design: dark window, Segoe UI, rounded owner-drawn buttons/cards/
 * progress, hover + press states, dark title bar (dwmapi, best effort).
 * All verbs are ui_-prefixed: no collisions with VexGUI/VexSYS.
 * Handles are small numbers (index+1), like VexGUI.
 * Cards sit BELOW siblings: create ui_card FIRST, then labels on top.
 */
#include "operator.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum { U_WIN = 0, U_CTL = 1 } UKind;
typedef enum {
    UC_LABEL = 0,
    UC_BTN = 1,
    UC_INPUT = 2,
    UC_CARD = 3,
    UC_PROG = 4,
    UC_SEP = 5
} UCtl;

typedef struct UObj {
    int alive;
    int kind;    /* UKind */
    int ctlkind; /* UCtl, for controls */
    HWND hwnd;
    COLORREF color;  /* accent/text color */
    int size;        /* font px for labels */
    int pct;         /* progress 0..100 */
    int hover;       /* polled hover flag for buttons */
    int clicked;     /* edge flag */
    HBRUSH bgb;      /* window bg brush (win only) */
    HBRUSH editb;    /* input bg brush (win only) */
} UObj;

static UObj *g_o = NULL;
static int g_n = 0, g_cap = 0;
static int g_cls = 0;
static HBRUSH g_bg = NULL;
static HFONT g_fonts[65];
static DWORD g_hover_at = 0;

#define U_BG RGB(24, 26, 34)
#define U_EDITBG RGB(34, 38, 48)
#define U_TRACK RGB(52, 58, 72)
#define U_LINE RGB(70, 78, 96)
#define U_TEXT RGB(232, 234, 240)
#define U_MUT RGB(154, 160, 174)

static int u_new(void) {
    for (int i = 0; i < g_n; i++) {
        if (!g_o[i].alive) {
            memset(&g_o[i], 0, sizeof(UObj));
            return i;
        }
    }
    if (g_n + 1 > g_cap) {
        int nc = g_cap ? g_cap * 2 : 16;
        UObj *nd = (UObj *)realloc(g_o, (size_t)nc * sizeof(UObj));
        if (!nd) return -1;
        g_o = nd;
        g_cap = nc;
    }
    memset(&g_o[g_n], 0, sizeof(UObj));
    return g_n++;
}

/* handle = index + 1 */
static int u_of(VxOpVal *h, const VxOpApi *api) {
    double d = 0;
    if (!api->as_num(h, &d)) return -1;
    long i = (long)d;
    if (i < 1 || i > g_n) return -1;
    if (!g_o[i - 1].alive) return -1;
    return (int)(i - 1);
}

static int u_find(HWND hwnd) {
    for (int i = 0; i < g_n; i++) {
        if (g_o[i].alive && g_o[i].hwnd == hwnd) return i;
    }
    return -1;
}

/* dark title bar via dwmapi loaded at runtime (no link dep) */
static void u_dark_title(HWND hwnd) {
    static HMODULE dw = NULL;
    static int tried = 0;
    typedef HRESULT(WINAPI *Fn)(HWND, DWORD, LPCVOID, DWORD);
    static Fn f = NULL;
    if (!tried) {
        tried = 1;
        dw = LoadLibraryW(L"dwmapi.dll");
        if (dw) f = (Fn)GetProcAddress(dw, "DwmSetWindowAttribute");
    }
    if (f) {
        BOOL v = TRUE;
        f(hwnd, 20, &v, sizeof(v));
    }
}

static HFONT u_font(int px, int bold) {
    int idx;
    if (px < 8) px = 8;
    if (px > 60) px = 60;
    idx = bold ? px : px + 0;
    /* two slots per size would waste; bold titles use size+64 trick below */
    (void)bold;
    if (!g_fonts[idx]) {
        g_fonts[idx] = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                   FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH, L"Segoe UI");
    }
    return g_fonts[idx];
}

static HFONT u_font_b(int px) {
    static HFONT b[65];
    if (px < 8) px = 8;
    if (px > 64) px = 64;
    if (!b[px]) {
        b[px] = CreateFontW(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
    }
    return b[px];
}

static COLORREF u_rgb(double v) {
    long c = (long)v;
    if (c < 0) c = 0;
    if (c > 0xFFFFFF) c = 0xFFFFFF;
    return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static COLORREF u_shade(COLORREF c, int d) {
    int r = GetRValue(c) + d, g = GetGValue(c) + d, b = GetBValue(c) + d;
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

/* poll hover for owner-drawn buttons (cheap, throttled) */
static void u_poll_hover(void) {
    DWORD now = GetTickCount();
    if (now - g_hover_at < 80) return;
    g_hover_at = now;
    POINT pt;
    if (!GetCursorPos(&pt)) return;
    for (int i = 0; i < g_n; i++) {
        if (!g_o[i].alive || g_o[i].kind != U_CTL ||
            g_o[i].ctlkind != UC_BTN)
            continue;
        RECT r;
        if (!GetWindowRect(g_o[i].hwnd, &r)) continue;
        int h = PtInRect(&r, pt);
        if (h != g_o[i].hover) {
            g_o[i].hover = h;
            InvalidateRect(g_o[i].hwnd, NULL, FALSE);
        }
    }
}

static void u_pump(void) {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    u_poll_hover();
}

static void u_draw_round(HDC dc, const RECT *r, COLORREF fill,
                         COLORREF edge, int rad) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, p);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void u_draw_btn(const DRAWITEMSTRUCT *di, UObj *o) {
    int hov = o->hover || (di->itemState & ODS_HOTLIGHT);
    COLORREF base = o->color;
    COLORREF fill = base;
    if (di->itemState & ODS_SELECTED) fill = u_shade(base, -34);
    else if (hov) fill = u_shade(base, 22);
    u_draw_round(di->hDC, &di->rcItem, fill, u_shade(base, 40), 14);
    /* top gloss */
    {
        RECT t = di->rcItem;
        t.bottom = t.top + (t.bottom - t.top) / 2;
        HBRUSH b = CreateSolidBrush(u_shade(fill, 18));
        HPEN p = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ ob = SelectObject(di->hDC, b);
        HGDIOBJ op = SelectObject(di->hDC, p);
        RoundRect(di->hDC, t.left + 3, t.top + 3, t.right - 3, t.bottom + 6,
                  10, 10);
        SelectObject(di->hDC, ob);
        SelectObject(di->hDC, op);
        DeleteObject(b);
        DeleteObject(p);
    }
    wchar_t txt[256];
    txt[0] = 0;
    GetWindowTextW(di->hwndItem, txt, 256);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, RGB(255, 255, 255));
    HFONT f = u_font_b(14);
    if (f) SelectObject(di->hDC, f);
    DrawTextW(di->hDC, txt, -1, (RECT *)&di->rcItem,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void u_draw_card(const DRAWITEMSTRUCT *di, UObj *o) {
    u_draw_round(di->hDC, &di->rcItem, o->color, u_shade(o->color, 22), 18);
}

static void u_draw_prog(const DRAWITEMSTRUCT *di, UObj *o) {
    RECT r = di->rcItem;
    u_draw_round(di->hDC, &r, U_TRACK, U_LINE, 12);
    int w = r.right - r.left - 8;
    int fill = (w * o->pct) / 100;
    if (fill > 0) {
        RECT f = r;
        f.left += 4;
        f.top += 4;
        f.bottom -= 4;
        f.right = f.left + fill;
        if (f.right > r.right - 4) f.right = r.right - 4;
        u_draw_round(di->hDC, &f, o->color, u_shade(o->color, 30), 8);
    }
}

static void u_draw_sep(const DRAWITEMSTRUCT *di) {
    RECT r = di->rcItem;
    int y = (r.top + r.bottom) / 2;
    HPEN p = CreatePen(PS_SOLID, 1, U_LINE);
    HGDIOBJ op = SelectObject(di->hDC, p);
    MoveToEx(di->hDC, r.left, y, NULL);
    LineTo(di->hDC, r.right, y);
    SelectObject(di->hDC, op);
    DeleteObject(p);
}

static LRESULT CALLBACK u_wnd(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        int ci = id - 100;
        if (ci >= 0 && ci < g_n && g_o[ci].alive &&
            g_o[ci].kind == U_CTL) {
            if (code == BN_CLICKED && g_o[ci].ctlkind == UC_BTN)
                g_o[ci].clicked = 1;
        }
        return 0;
    }
    if (msg == WM_DRAWITEM) {
        const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lp;
        int ci = (int)di->CtlID - 100;
        if (ci >= 0 && ci < g_n && g_o[ci].alive &&
            g_o[ci].kind == U_CTL) {
            UObj *o = &g_o[ci];
            if (o->ctlkind == UC_BTN) u_draw_btn(di, o);
            else if (o->ctlkind == UC_CARD) u_draw_card(di, o);
            else if (o->ctlkind == UC_PROG) u_draw_prog(di, o);
            else if (o->ctlkind == UC_SEP) u_draw_sep(di);
            return TRUE;
        }
        return FALSE;
    }
    if (msg == WM_CTLCOLORSTATIC) {
        HDC dc = (HDC)wp;
        int ci = GetDlgCtrlID((HWND)lp) - 100;
        COLORREF fg = U_TEXT;
        if (ci >= 0 && ci < g_n && g_o[ci].alive &&
            g_o[ci].kind == U_CTL)
            fg = g_o[ci].color;
        SetTextColor(dc, fg);
        SetBkMode(dc, TRANSPARENT);
        return (INT_PTR)g_bg;
    }
    if (msg == WM_CTLCOLOREDIT) {
        HDC dc = (HDC)wp;
        int ci = GetDlgCtrlID((HWND)lp) - 100;
        int wi = u_find(hwnd);
        SetTextColor(dc, U_TEXT);
        SetBkColor(dc, U_EDITBG);
        if (wi >= 0 && g_o[wi].editb) return (INT_PTR)g_o[wi].editb;
        return (INT_PTR)g_bg;
    }
    if (msg == WM_ERASEBKGND) {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wp, &r, g_bg);
        return 1;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        int wi = u_find(hwnd);
        if (wi >= 0) {
            g_o[wi].alive = 0;
            if (g_o[wi].bgb) DeleteObject(g_o[wi].bgb);
            if (g_o[wi].editb) DeleteObject(g_o[wi].editb);
            g_o[wi].bgb = g_o[wi].editb = NULL;
            for (int i = 0; i < g_n; i++) {
                if (g_o[i].alive && g_o[i].kind == U_CTL &&
                    GetParent(g_o[i].hwnd) == hwnd)
                    g_o[i].alive = 0;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int u_class(void) {
    if (g_cls) return 1;
    if (!g_bg) g_bg = CreateSolidBrush(U_BG);
    if (!g_bg) return 0;
    WNDCLASSW kc;
    memset(&kc, 0, sizeof(kc));
    kc.lpfnWndProc = u_wnd;
    kc.hInstance = GetModuleHandleW(NULL);
    kc.lpszClassName = L"VexUIWindow";
    kc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    kc.hbrBackground = g_bg;
    if (!RegisterClassW(&kc)) return 0;
    g_cls = 1;
    return 1;
}

static wchar_t *u_w(const char *s, size_t len) {
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

static char *u_u8(const wchar_t *w, int n, size_t *out_n) {
    int m = WideCharToMultiByte(CP_UTF8, 0, w, n, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)(m > 0 ? m : 1));
    if (!s) return NULL;
    if (m > 0) WideCharToMultiByte(CP_UTF8, 0, w, n, s, m, NULL, NULL);
    if (out_n) *out_n = (size_t)(m > 0 ? m : 0);
    return s;
}

/* ---------------- verbs ---------------- */

static int u_window(const VxOpApi *api, VxOpVal **a, int argc,
                    VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double w = 0, h = 0;
    if (!api->as_text(a[0], &t, &tl) || !api->as_num(a[1], &w) ||
        !api->as_num(a[2], &h))
        return 1;
    if (!u_class()) return 1;
    wchar_t *wt = u_w(t, tl);
    if (!wt) return 1;
    int wi = (int)w, hi = (int)h;
    if (wi < 200) wi = 200;
    if (hi < 120) hi = 120;
    if (wi > 4096) wi = 4096;
    if (hi > 4096) hi = 4096;
    HWND hwnd = CreateWindowExW(0, L"VexUIWindow", wt,
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, wi, hi, NULL,
                                NULL, GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!hwnd) return 1;
    u_dark_title(hwnd);
    int idx = u_new();
    if (idx < 0) {
        DestroyWindow(hwnd);
        return 1;
    }
    g_o[idx].alive = 1;
    g_o[idx].kind = U_WIN;
    g_o[idx].hwnd = hwnd;
    g_o[idx].bgb = CreateSolidBrush(U_BG);
    g_o[idx].editb = CreateSolidBrush(U_EDITBG);
    *out = api->make_num((double)(idx + 1));
    return *out ? 0 : 1;
}

static int u_need_win(const VxOpApi *api, VxOpVal *h, int *idx) {
    int i = u_of(h, api);
    if (i < 0 || g_o[i].kind != U_WIN) return 0;
    *idx = i;
    return 1;
}

static int u_need_ctl(const VxOpApi *api, VxOpVal *h, int ck, int *idx) {
    int i = u_of(h, api);
    if (i < 0 || g_o[i].kind != U_CTL) return 0;
    if (ck >= 0 && g_o[i].ctlkind != ck) return 0;
    *idx = i;
    return 1;
}

static int u_show(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!u_need_win(api, a[0], &i)) return 1;
    ShowWindow(g_o[i].hwnd, SW_SHOW);
    UpdateWindow(g_o[i].hwnd);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

static int u_open(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!u_need_win(api, a[0], &i)) {
        *out = api->make_num(0);
        return *out ? 0 : 1;
    }
    u_pump();
    *out = api->make_num(g_o[i].alive ? 1 : 0);
    return *out ? 0 : 1;
}

static int u_close(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!u_need_win(api, a[0], &i)) return 1;
    g_o[i].alive = 0;
    DestroyWindow(g_o[i].hwnd);
    u_pump();
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

static int u_dark(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!u_need_win(api, a[0], &i)) return 1;
    u_dark_title(g_o[i].hwnd);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* shared child factory; dw/dh<=0 mean auto */
static int u_child(const VxOpApi *api, VxOpVal *win, const wchar_t *cls,
                   DWORD style, const char *txt, size_t txtlen, double x,
                   double y, double dw, double dh, int ctlkind,
                   VxOpVal **out) {
    int wi = 0;
    if (!u_need_win(api, win, &wi)) return 1;
    wchar_t *wt = u_w(txt, txtlen);
    if (!wt) return 1;
    int idx = u_new();
    if (idx < 0) {
        free(wt);
        return 1;
    }
    HWND hwnd = CreateWindowExW(0, cls, wt,
                                WS_CHILD | WS_VISIBLE | style, (int)x,
                                (int)y, (int)dw, (int)dh, g_o[wi].hwnd,
                                (HMENU)(INT_PTR)(100 + idx),
                                GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!hwnd) {
        g_o[idx].alive = 0;
        return 1;
    }
    g_o[idx].alive = 1;
    g_o[idx].kind = U_CTL;
    g_o[idx].ctlkind = ctlkind;
    g_o[idx].hwnd = hwnd;
    *out = api->make_num((double)(idx + 1));
    return *out ? 0 : 1;
}

/* ui_title # w, text, x, y, size — big bold header */
static int u_title(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double x = 0, y = 0, sz = 0;
    if (!api->as_text(a[1], &t, &tl) || !api->as_num(a[2], &x) ||
        !api->as_num(a[3], &y) || !api->as_num(a[4], &sz))
        return 1;
    if (sz < 12) sz = 12;
    if (sz > 60) sz = 60;
    if (u_child(api, a[0], L"STATIC", SS_LEFT, t, tl, x, y, 560, (int)sz + 14,
                UC_LABEL, out))
        return 1;
    /* out holds handle number (index+1); resolve back to slot */
    {
        double d = 0;
        api->as_num(*out, &d);
        int idx = (int)(long)d - 1;
        g_o[idx].color = U_TEXT;
        SendMessageW(g_o[idx].hwnd, WM_SETFONT, (WPARAM)u_font_b((int)sz),
                     1);
    }
    return 0;
}

/* ui_label # w, text, x, y, color, size */
static int u_label(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double x = 0, y = 0, c = 0, sz = 0;
    if (!api->as_text(a[1], &t, &tl) || !api->as_num(a[2], &x) ||
        !api->as_num(a[3], &y) || !api->as_num(a[4], &c) ||
        !api->as_num(a[5], &sz))
        return 1;
    if (sz < 8) sz = 8;
    if (sz > 60) sz = 60;
    if (u_child(api, a[0], L"STATIC", SS_LEFT, t, tl, x, y, 560, (int)sz + 12,
                UC_LABEL, out))
        return 1;
    {
        double d = 0;
        api->as_num(*out, &d);
        int idx = (int)(long)d - 1;
        g_o[idx].color = u_rgb(c);
        SendMessageW(g_o[idx].hwnd, WM_SETFONT, (WPARAM)u_font((int)sz, 0),
                     1);
    }
    return 0;
}

/* ui_btn # w, text, x, y, ww, hh, color — owner-drawn pill button */
static int u_btn(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double x = 0, y = 0, ww = 0, hh = 0, c = 0;
    if (!api->as_text(a[1], &t, &tl) || !api->as_num(a[2], &x) ||
        !api->as_num(a[3], &y) || !api->as_num(a[4], &ww) ||
        !api->as_num(a[5], &hh) || !api->as_num(a[6], &c))
        return 1;
    if (ww < 40) ww = 40;
    if (hh < 24) hh = 24;
    if (u_child(api, a[0], L"BUTTON", BS_OWNERDRAW, t, tl, x, y, ww, hh,
                UC_BTN, out))
        return 1;
    {
        double d = 0;
        api->as_num(*out, &d);
        g_o[(int)(long)d - 1].color = u_rgb(c);
    }
    return 0;
}

/* ui_input # w, x, y, ww — dark single-line field */
static int u_input(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0, ww = 0;
    int idx = 0;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &ww))
        return 1;
    if (ww < 40) ww = 40;
    if (u_child(api, a[0], L"EDIT", WS_BORDER | ES_AUTOHSCROLL | ES_LEFT,
                "", 0, x, y, ww, 30, UC_INPUT, out))
        return 1;
    {
        double d = 0;
        api->as_num(*out, &d);
        idx = (int)(long)d - 1;
    }
    SendMessageW(g_o[idx].hwnd, WM_SETFONT, (WPARAM)u_font(14, 0), 1);
    /* kill the 3D border: flat dark edge */
    SetWindowLongW(g_o[idx].hwnd, GWL_EXSTYLE,
                   GetWindowLongW(g_o[idx].hwnd, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
    SetWindowPos(g_o[idx].hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    return 0;
}

/* ui_card # w, x, y, ww, hh, color — rounded panel, create FIRST */
static int u_card(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0, ww = 0, hh = 0, c = 0;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &ww) || !api->as_num(a[4], &hh) ||
        !api->as_num(a[5], &c))
        return 1;
    if (ww < 40) ww = 40;
    if (hh < 24) hh = 24;
    if (u_child(api, a[0], L"STATIC", SS_OWNERDRAW, "", 0, x, y, ww, hh,
                UC_CARD, out))
        return 1;
    {
        double d = 0;
        api->as_num(*out, &d);
        g_o[(int)(long)d - 1].color = u_rgb(c);
    }
    return 0;
}

/* ui_prog # w, x, y, ww, color — track bar, value via ui_pset */
static int u_prog(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0, ww = 0, c = 0;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &ww) || !api->as_num(a[4], &c))
        return 1;
    if (ww < 40) ww = 40;
    if (u_child(api, a[0], L"STATIC", SS_OWNERDRAW, "", 0, x, y, ww, 16,
                UC_PROG, out))
        return 1;
    {
        double d = 0;
        api->as_num(*out, &d);
        int idx = (int)(long)d - 1;
        g_o[idx].color = u_rgb(c);
        g_o[idx].pct = 0;
    }
    return 0;
}

/* ui_pset # h, pct */
static int u_pset(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double p = 0;
    if (!u_need_ctl(api, a[0], UC_PROG, &i)) return 1;
    if (!api->as_num(a[1], &p)) return 1;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    g_o[i].pct = (int)p;
    InvalidateRect(g_o[i].hwnd, NULL, FALSE);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* ui_sep # w, x, y, ww — thin divider line */
static int u_sep(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0, ww = 0;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &ww))
        return 1;
    if (ww < 20) ww = 20;
    return u_child(api, a[0], L"STATIC", SS_OWNERDRAW, "", 0, x, y, ww, 8,
                   UC_SEP, out);
}

/* ui_get # h — window text of any control */
static int u_get(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!u_need_ctl(api, a[0], -1, &i)) return 1;
    int n = GetWindowTextLengthW(g_o[i].hwnd);
    if (n < 0) return 1;
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
    if (!w) return 1;
    GetWindowTextW(g_o[i].hwnd, w, n + 1);
    size_t m = 0;
    char *s = u_u8(w, n, &m);
    free(w);
    if (!s) return 1;
    *out = api->make_text(s, m);
    free(s);
    return *out ? 0 : 1;
}

/* ui_set # h, text — set + repaint (owner-drawn need it) */
static int u_set(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    const char *t = NULL;
    size_t tl = 0;
    if (!u_need_ctl(api, a[0], -1, &i)) return 1;
    if (!api->as_text(a[1], &t, &tl)) return 1;
    wchar_t *wt = u_w(t, tl);
    if (!wt) return 1;
    BOOL ok = SetWindowTextW(g_o[i].hwnd, wt);
    free(wt);
    if (!ok) return 1;
    InvalidateRect(g_o[i].hwnd, NULL, FALSE);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* ui_clicked # h — edge click, one-shot */
static int u_clicked(const VxOpApi *api, VxOpVal **a, int argc,
                     VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!u_need_ctl(api, a[0], UC_BTN, &i)) return 1;
    u_pump();
    int hit = g_o[i].alive && g_o[i].clicked;
    g_o[i].clicked = 0;
    *out = api->make_num(hit ? 1 : 0);
    return *out ? 0 : 1;
}

/* ui_run # w, "step" — message loop, step wet = keep going */
static int u_run(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!u_need_win(api, a[0], &i)) return 1;
    const char *s = NULL;
    size_t sl = 0;
    if (!api->as_text(a[1], &s, &sl) || sl == 0 || sl > 48) return 1;
    char step[64];
    memcpy(step, s, sl);
    step[sl] = '\0';
    if (!api->summon) return 1;
    for (;;) {
        u_pump();
        if (!g_o[i].alive || !IsWindow(g_o[i].hwnd)) break;
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
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "ui_window", 3, u_window },
    { "ui_show", 1, u_show },
    { "ui_open", 1, u_open },
    { "ui_close", 1, u_close },
    { "ui_dark", 1, u_dark },
    { "ui_run", 2, u_run },
    { "ui_title", 5, u_title },
    { "ui_label", 6, u_label },
    { "ui_btn", 7, u_btn },
    { "ui_input", 4, u_input },
    { "ui_card", 6, u_card },
    { "ui_prog", 5, u_prog },
    { "ui_pset", 2, u_pset },
    { "ui_sep", 4, u_sep },
    { "ui_get", 1, u_get },
    { "ui_set", 2, u_set },
    { "ui_clicked", 1, u_clicked },
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
    info.name = "VexUI";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
