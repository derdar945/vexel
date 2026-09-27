/* VexGame — tiny game operator for Vexel. Win32 GDI, no frameworks.
 *
 * Bench view (same loop idea as VexGUI: operator owns the window,
 * Vexel owns the step; state lives in sprites + slots, because
 * `@` inside a tool is local):
 *
 *   @ VexGame
 *   @ g : make # "Hi", 480, 320
 *   @ bg : color # 12, 12, 24
 *   @ b : spr # g, 40, 60, 28, 28, color # 255, 90, 90
 *   & step :
 *     cls # g, bg
 *     move # b, 2, 1
 *     draw # g
 *     flip # g
 *     = open # g
 *   .
 *   > show # g
 *   > run # g, "step"
 */
#define _CRT_SECURE_NO_WARNINGS
#include "operator.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct Game {
    int alive;
    HWND hwnd;
    HDC front;
    HDC back;
    HBITMAP bmp;
    HBITMAP old;
    int W, H;
    ULONGLONG last;
    double fps;
    int dt;
    long frame;
    BYTE held[256];
    BYTE once[256];
    int mx, my;
    int mdown;
    int mclick;
    double slots[16];
} Game;

typedef struct Spr {
    int alive;
    int owner; /* game index, 0-based */
    double x, y, w, h;
    COLORREF col;
} Spr;

static Game *g_games = NULL;
static int g_ngames = 0, g_cgames = 0;
static Spr *g_sprs = NULL;
static int g_nsprs = 0, g_csprs = 0;
static int g_class_ok = 0;

static int new_game(void) {
    for (int i = 0; i < g_ngames; i++) {
        if (!g_games[i].alive) {
            memset(&g_games[i], 0, sizeof(Game));
            return i;
        }
    }
    if (g_ngames + 1 > g_cgames) {
        int nc = g_cgames ? g_cgames * 2 : 8;
        Game *nd = (Game *)realloc(g_games, (size_t)nc * sizeof(Game));
        if (!nd) return -1;
        g_games = nd;
        g_cgames = nc;
    }
    memset(&g_games[g_ngames], 0, sizeof(Game));
    return g_ngames++;
}

static int new_spr(void) {
    for (int i = 0; i < g_nsprs; i++) {
        if (!g_sprs[i].alive) {
            memset(&g_sprs[i], 0, sizeof(Spr));
            return i;
        }
    }
    if (g_nsprs + 1 > g_csprs) {
        int nc = g_csprs ? g_csprs * 2 : 32;
        Spr *nd = (Spr *)realloc(g_sprs, (size_t)nc * sizeof(Spr));
        if (!nd) return -1;
        g_sprs = nd;
        g_csprs = nc;
    }
    memset(&g_sprs[g_nsprs], 0, sizeof(Spr));
    return g_nsprs++;
}

/* handle = index + 1 (0 never valid) */
static int game_of(VxOpVal *h, const VxOpApi *api) {
    double d = 0;
    if (!api->as_num(h, &d)) return -1;
    long i = (long)d;
    if (i < 1 || i > g_ngames) return -1;
    if (!g_games[i - 1].alive) return -1;
    return (int)(i - 1);
}

static int spr_of(VxOpVal *h, const VxOpApi *api) {
    double d = 0;
    if (!api->as_num(h, &d)) return -1;
    long i = (long)d;
    if (i < 1 || i > g_nsprs) return -1;
    if (!g_sprs[i - 1].alive) return -1;
    return (int)(i - 1);
}

static void pump(void) {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static void free_gdi(int gi) {
    Game *g = &g_games[gi];
    if (g->back) {
        if (g->old) SelectObject(g->back, g->old);
        DeleteDC(g->back);
        g->back = NULL;
    }
    if (g->bmp) {
        DeleteObject(g->bmp);
        g->bmp = NULL;
    }
    g->old = NULL;
    if (g->front && g->hwnd) {
        ReleaseDC(g->hwnd, g->front);
        g->front = NULL;
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    int gi = (int)(INT_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    Game *g = (gi >= 0 && gi < g_ngames) ? &g_games[gi] : NULL;
    switch (msg) {
        case WM_KEYDOWN: {
            if (g && wp < 256) {
                if (!(lp & (1 << 30))) g->once[wp] = 1; /* no auto-repeat */
                g->held[wp] = 1;
            }
            return 0;
        }
        case WM_KEYUP: {
            if (g && wp < 256) g->held[wp] = 0;
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g) {
                g->mx = (int)(short)LOWORD(lp);
                g->my = (int)(short)HIWORD(lp);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (g) {
                g->mdown = 1;
                g->mclick = 1;
                g->mx = (int)(short)LOWORD(lp);
                g->my = (int)(short)HIWORD(lp);
                SetFocus(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g) g->mdown = 0;
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            if (g && g->back) {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(hwnd, &ps);
                BitBlt(dc, 0, 0, g->W, g->H, g->back, 0, 0, SRCCOPY);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;
        }
        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            if (g) {
                g->alive = 0;
                free_gdi(gi);
                g->hwnd = NULL;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int ensure_class(void) {
    if (g_class_ok) return 1;
    WNDCLASSW kc;
    memset(&kc, 0, sizeof(kc));
    kc.lpfnWndProc = wnd_proc;
    kc.hInstance = GetModuleHandleW(NULL);
    kc.lpszClassName = L"VexGameWindow";
    kc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    if (!RegisterClassW(&kc)) return 0;
    g_class_ok = 1;
    return 1;
}

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

static char *w_to_u8_n(const wchar_t *w, DWORD nchars) {
    if (nchars == 0) {
        char *s = (char *)malloc(1);
        if (s) s[0] = 0;
        return s;
    }
    int m = WideCharToMultiByte(CP_UTF8, 0, w, (int)nchars, NULL, 0, NULL, NULL);
    if (m <= 0) return NULL;
    char *s = (char *)malloc((size_t)m + 1);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, (int)nchars, s, m, NULL, NULL);
    s[m] = 0;
    return s;
}

static VxOpVal *ok_num(const VxOpApi *api, double d) {
    return api->make_num(d);
}

static int need_game(const VxOpApi *api, VxOpVal *h, int *idx) {
    int i = game_of(h, api);
    if (i < 0) return 0;
    *idx = i;
    return 1;
}

static int need_spr(const VxOpApi *api, VxOpVal *h, int *idx) {
    int i = spr_of(h, api);
    if (i < 0) return 0;
    *idx = i;
    return 1;
}

static COLORREF col_of(double d) {
    unsigned long c = (unsigned long)(long)d;
    return (COLORREF)(c & 0xFFFFFFUL);
}

static HBRUSH brush_of(COLORREF c) {
    return CreateSolidBrush(c);
}

/* ---------------- verbs ---------------- */

static int g_version(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_text("1.0.0", 5);
    return *out ? 0 : 1;
}

static int make_impl(const VxOpApi *api, const char *t, size_t tl,
                     int W, int H, VxOpVal **out) {
    if (!ensure_class()) return 1;
    if (W < 16) W = 16;
    if (H < 16) H = 16;
    if (W > 2048) W = 2048;
    if (H > 2048) H = 2048;
    wchar_t *wt = u8_to_w(t, tl);
    if (!wt) return 1;
    RECT rc = {0, 0, W, H};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowExW(0, L"VexGameWindow", wt,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                NULL, NULL, GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!hwnd) return 1;
    int idx = new_game();
    if (idx < 0) {
        DestroyWindow(hwnd);
        return 1;
    }
    Game *g = &g_games[idx];
    g->alive = 1;
    g->hwnd = hwnd;
    g->W = W;
    g->H = H;
    g->front = GetDC(hwnd);
    if (!g->front) {
        g->alive = 0;
        DestroyWindow(hwnd);
        return 1;
    }
    g->back = CreateCompatibleDC(g->front);
    if (!g->back) {
        free_gdi(idx);
        g->alive = 0;
        DestroyWindow(hwnd);
        return 1;
    }
    g->bmp = CreateCompatibleBitmap(g->front, W, H);
    if (!g->bmp) {
        free_gdi(idx);
        g->alive = 0;
        DestroyWindow(hwnd);
        return 1;
    }
    g->old = (HBITMAP)SelectObject(g->back, g->bmp);
    /* paint black so the first flip is never garbage */
    RECT full = {0, 0, W, H};
    FillRect(g->back, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));
    g->last = 0;
    g->fps = 60.0;
    g->dt = 16;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)idx);
    *out = ok_num(api, (double)(idx + 1));
    return *out ? 0 : 1;
}

/* make # title, w, h — hidden game window + backbuffer */
static int g_make(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double w = 0, h = 0;
    if (!api->as_text(a[0], &t, &tl)) return 1;
    if (!api->as_num(a[1], &w) || !api->as_num(a[2], &h)) return 1;
    return make_impl(api, t, tl, (int)w, (int)h, out);
}

/* game # w, h, title — dots-style alias of make (note arg order) */
static int g_game_alias(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double w = 0, h = 0;
    if (!api->as_num(a[0], &w) || !api->as_num(a[1], &h)) return 1;
    if (!api->as_text(a[2], &t, &tl)) return 1;
    return make_impl(api, t, tl, (int)w, (int)h, out);
}

static int g_show(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_game(api, a[0], &i)) return 1;
    ShowWindow(g_games[i].hwnd, SW_SHOW);
    UpdateWindow(g_games[i].hwnd);
    SetFocus(g_games[i].hwnd);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int g_open(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_game(api, a[0], &i)) {
        /* dead game reads as closed, not as crash (like VexGUI) */
        *out = ok_num(api, 0);
        return *out ? 0 : 1;
    }
    pump();
    *out = ok_num(api, g_games[i].alive ? 1 : 0);
    return *out ? 0 : 1;
}

static int g_close(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_game(api, a[0], &i)) return 1;
    g_games[i].alive = 0;
    if (g_games[i].hwnd) DestroyWindow(g_games[i].hwnd);
    pump();
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* run # g, "step" — 60fps loop; step answers wet to keep going.
 * frame # g, "step" — same loop (dots-style name). */
static int run_loop(const VxOpApi *api, int gi, const char *step, VxOpVal **out) {
    if (!api->summon) return 1;
    for (;;) {
        ULONGLONG frame0 = GetTickCount64();
        pump();
        if (!g_games[gi].alive || !g_games[gi].hwnd || !IsWindow(g_games[gi].hwnd)) break;
        ULONGLONG now = GetTickCount64();
        if (g_games[gi].last == 0) {
            g_games[gi].dt = 16;
        } else {
            ULONGLONG e = now - g_games[gi].last;
            if (e > 250) e = 250;
            g_games[gi].dt = (int)e;
            if (g_games[gi].dt > 0) {
                double inst = 1000.0 / (double)g_games[gi].dt;
                g_games[gi].fps = g_games[gi].fps * 0.9 + inst * 0.1;
            }
        }
        g_games[gi].last = now;
        g_games[gi].frame++;
        VxOpVal *r = NULL;
        if (api->summon(step, NULL, 0, &r) != 0) {
            if (r) api->release(r);
            return 1;
        }
        int keep = api->is_true(r);
        if (r) api->release(r);
        if (!keep) break;
        ULONGLONG spent = GetTickCount64() - frame0;
        if (spent < 16) Sleep((DWORD)(16 - spent));
    }
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int loop_arg(const VxOpApi *api, VxOpVal **a, int *gi, char *step) {
    if (!need_game(api, a[0], gi)) return 0;
    const char *s = NULL;
    size_t sl = 0;
    if (!api->as_text(a[1], &s, &sl) || sl == 0 || sl > 48) return 0;
    memcpy(step, s, sl);
    step[sl] = '\0';
    return 1;
}

static int g_run(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    char step[64];
    if (!loop_arg(api, a, &gi, step)) return 1;
    return run_loop(api, gi, step, out);
}

/* frame # g, "step" — dots-style alias of run */
static int g_frame_loop(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    char step[64];
    if (!loop_arg(api, a, &gi, step)) return 1;
    return run_loop(api, gi, step, out);
}

/* size # g — [w h] client vec */
static int g_size(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_game(api, a[0], &i)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    VxOpVal *w = ok_num(api, (double)g_games[i].W);
    VxOpVal *h = ok_num(api, (double)g_games[i].H);
    if (!w || !h || api->vec_push(v, w) != 0 || api->vec_push(v, h) != 0) {
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

/* dt # g — ms since last frame; fps # g — smoothed frames per second */
static int g_dt(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_game(api, a[0], &i)) return 1;
    *out = ok_num(api, (double)g_games[i].dt);
    return *out ? 0 : 1;
}

static int g_fps(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_game(api, a[0], &i)) return 1;
    *out = ok_num(api, g_games[i].fps);
    return *out ? 0 : 1;
}

/* tick # g — frames run so far (dots-style stateless motion: x = f(tick)) */
static int g_tick(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_game(api, a[0], &i)) return 1;
    pump();
    *out = ok_num(api, (double)g_games[i].frame);
    return *out ? 0 : 1;
}

static int do_cls(const VxOpApi *api, int gi, double c) {
    RECT full = {0, 0, g_games[gi].W, g_games[gi].H};
    HBRUSH b = brush_of(col_of(c));
    if (!b) return 0;
    FillRect(g_games[gi].back, &full, b);
    DeleteObject(b);
    (void)api;
    return 1;
}

/* cls # g, color — fill backbuffer */
static int g_cls(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double c = 0;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &c)) return 1;
    if (!do_cls(api, i, c)) return 1;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* fill # g, color — dots-style alias of cls */
static int g_fill(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double c = 0;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &c)) return 1;
    if (!do_cls(api, i, c)) return 1;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* flip # g — present backbuffer */
static int g_flip(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_game(api, a[0], &i)) return 1;
    if (!BitBlt(g_games[i].front, 0, 0, g_games[i].W, g_games[i].H,
                g_games[i].back, 0, 0, SRCCOPY))
        return 1;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* rect # g, x, y, w, h, color — filled box */
static int g_rect(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x, y, w, h, c;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &w) || !api->as_num(a[4], &h) ||
        !api->as_num(a[5], &c))
        return 1;
    RECT r = {(int)x, (int)y, (int)(x + w), (int)(y + h)};
    HBRUSH b = brush_of(col_of(c));
    if (!b) return 1;
    FillRect(g_games[i].back, &r, b);
    DeleteObject(b);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* box # g, x, y, w, h, color — outline box */
static int g_box(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x, y, w, h, c;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &w) || !api->as_num(a[4], &h) ||
        !api->as_num(a[5], &c))
        return 1;
    RECT r = {(int)x, (int)y, (int)(x + w), (int)(y + h)};
    HBRUSH b = brush_of(col_of(c));
    if (!b) return 1;
    FrameRect(g_games[i].back, &r, b);
    DeleteObject(b);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* circle # g, x, y, r, color — filled disc */
static int g_circle(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x, y, r, c;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &r) || !api->as_num(a[4], &c))
        return 1;
    if (r < 0) return 1;
    HBRUSH b = brush_of(col_of(c));
    if (!b) return 1;
    HGDIOBJ oldB = SelectObject(g_games[i].back, b);
    HGDIOBJ oldP = SelectObject(g_games[i].back, GetStockObject(NULL_PEN));
    Ellipse(g_games[i].back, (int)(x - r), (int)(y - r), (int)(x + r), (int)(y + r));
    SelectObject(g_games[i].back, oldP);
    SelectObject(g_games[i].back, oldB);
    DeleteObject(b);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* ring # g, x, y, r, color — outline disc */
static int g_ring(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x, y, r, c;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &r) || !api->as_num(a[4], &c))
        return 1;
    if (r < 0) return 1;
    HPEN p = CreatePen(PS_SOLID, 2, col_of(c));
    if (!p) return 1;
    HGDIOBJ oldP = SelectObject(g_games[i].back, p);
    HGDIOBJ oldB = SelectObject(g_games[i].back, GetStockObject(NULL_BRUSH));
    Ellipse(g_games[i].back, (int)(x - r), (int)(y - r), (int)(x + r), (int)(y + r));
    SelectObject(g_games[i].back, oldB);
    SelectObject(g_games[i].back, oldP);
    DeleteObject(p);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* line # g, x1, y1, x2, y2, color */
static int g_line(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x1, y1, x2, y2, c;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &x1) || !api->as_num(a[2], &y1) ||
        !api->as_num(a[3], &x2) || !api->as_num(a[4], &y2) ||
        !api->as_num(a[5], &c))
        return 1;
    HPEN p = CreatePen(PS_SOLID, 1, col_of(c));
    if (!p) return 1;
    HGDIOBJ old = SelectObject(g_games[i].back, p);
    MoveToEx(g_games[i].back, (int)x1, (int)y1, NULL);
    LineTo(g_games[i].back, (int)x2, (int)y2);
    SelectObject(g_games[i].back, old);
    DeleteObject(p);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int do_dot(int gi, double x, double y, double c) {
    SetPixel(g_games[gi].back, (int)x, (int)y, col_of(c));
    return 1;
}

/* dot # g, x, y, color — single pixel */
static int g_dot(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x, y, c;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &c))
        return 1;
    do_dot(i, x, y, c);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* getpx # g, x, y — backbuffer pixel color number, fail when outside */
static int g_getpx(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x, y;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y)) return 1;
    int xi = (int)x, yi = (int)y;
    if (xi < 0 || yi < 0 || xi >= g_games[i].W || yi >= g_games[i].H) return 1;
    COLORREF c = GetPixel(g_games[i].back, xi, yi);
    if (c == CLR_INVALID) return 1;
    *out = ok_num(api, (double)(c & 0xFFFFFFUL));
    return *out ? 0 : 1;
}

/* px # g, x, y, color — dots-style alias of dot */
static int g_px(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x, y, c;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &c))
        return 1;
    do_dot(i, x, y, c);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* text # g, s, x, y, color */
static int g_text(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    const char *s = NULL;
    size_t n = 0;
    double x, y, c;
    if (!need_game(api, a[0], &i)) return 1;
    if (!api->as_text(a[1], &s, &n)) return 1;
    if (!api->as_num(a[2], &x) || !api->as_num(a[3], &y) ||
        !api->as_num(a[4], &c))
        return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    SetTextColor(g_games[i].back, col_of(c));
    SetBkMode(g_games[i].back, TRANSPARENT);
    HGDIOBJ old = SelectObject(g_games[i].back, GetStockObject(DEFAULT_GUI_FONT));
    TextOutW(g_games[i].back, (int)x, (int)y, w, (int)wcslen(w));
    SelectObject(g_games[i].back, old);
    free(w);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* color # r, g, b — build a paint number (0-255 each) */
static int g_color(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double r, g, b;
    if (!api->as_num(a[0], &r) || !api->as_num(a[1], &g) ||
        !api->as_num(a[2], &b))
        return 1;
    int ri = (int)r, gi = (int)g, bi = (int)b;
    if (ri < 0) ri = 0; if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; if (bi > 255) bi = 255;
    *out = ok_num(api, (double)RGB(ri, gi, bi));
    return *out ? 0 : 1;
}

/* ---------------- sprites (native state) ---------------- */

/* spr # g, x, y, w, h, color — new sprite id */
static int g_spr(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double x, y, w, h, c;
    if (!need_game(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &w) || !api->as_num(a[4], &h) ||
        !api->as_num(a[5], &c))
        return 1;
    if (w <= 0 || h <= 0) return 1;
    int si = new_spr();
    if (si < 0) return 1;
    g_sprs[si].alive = 1;
    g_sprs[si].owner = gi;
    g_sprs[si].x = x;
    g_sprs[si].y = y;
    g_sprs[si].w = w;
    g_sprs[si].h = h;
    g_sprs[si].col = col_of(c);
    *out = ok_num(api, (double)(si + 1));
    return *out ? 0 : 1;
}

/* move # id, dx, dy — relative step */
static int g_move(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int si = 0;
    double dx, dy;
    if (!need_spr(api, a[0], &si)) return 1;
    if (!api->as_num(a[1], &dx) || !api->as_num(a[2], &dy)) return 1;
    g_sprs[si].x += dx;
    g_sprs[si].y += dy;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* place # id, x, y — absolute step */
static int g_place(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int si = 0;
    double x, y;
    if (!need_spr(api, a[0], &si)) return 1;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y)) return 1;
    g_sprs[si].x = x;
    g_sprs[si].y = y;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* pos # id — [x y] */
static int g_pos(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int si = 0;
    if (!need_spr(api, a[0], &si)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    VxOpVal *x = ok_num(api, g_sprs[si].x);
    VxOpVal *y = ok_num(api, g_sprs[si].y);
    if (!x || !y || api->vec_push(v, x) != 0 || api->vec_push(v, y) != 0) {
        if (x) api->release(x);
        if (y) api->release(y);
        api->release(v);
        return 1;
    }
    api->release(x);
    api->release(y);
    *out = v;
    return 0;
}

/* spr_box # id — [x y w h] */
static int g_spr_box(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int si = 0;
    if (!need_spr(api, a[0], &si)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    VxOpVal *items[4];
    items[0] = ok_num(api, g_sprs[si].x);
    items[1] = ok_num(api, g_sprs[si].y);
    items[2] = ok_num(api, g_sprs[si].w);
    items[3] = ok_num(api, g_sprs[si].h);
    for (int k = 0; k < 4; k++) {
        if (!items[k] || api->vec_push(v, items[k]) != 0) {
            for (int j = 0; j < 4; j++)
                if (items[j]) api->release(items[j]);
            api->release(v);
            return 1;
        }
    }
    for (int k = 0; k < 4; k++) api->release(items[k]);
    *out = v;
    return 0;
}

/* paint # id, color — recolor (hit flash) */
static int g_paint(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int si = 0;
    double c = 0;
    if (!need_spr(api, a[0], &si)) return 1;
    if (!api->as_num(a[1], &c)) return 1;
    g_sprs[si].col = col_of(c);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* kill # id — destroy sprite (bullets, eaten dots) */
static int g_kill(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int si = 0;
    if (!need_spr(api, a[0], &si)) return 1;
    g_sprs[si].alive = 0;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int rects_hit(double ax, double ay, double aw, double ah,
                     double bx, double by, double bw, double bh) {
    return (ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by) ? 1 : 0;
}

/* touch # a, b — sprite AABB overlap, 1/0 */
static int g_touch(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int sa = 0, sb = 0;
    if (!need_spr(api, a[0], &sa)) return 1;
    if (!need_spr(api, a[1], &sb)) return 1;
    int hit = rects_hit(g_sprs[sa].x, g_sprs[sa].y, g_sprs[sa].w, g_sprs[sa].h,
                        g_sprs[sb].x, g_sprs[sb].y, g_sprs[sb].w, g_sprs[sb].h);
    *out = ok_num(api, hit);
    return *out ? 0 : 1;
}

/* draw # g — paint every alive sprite of this game */
static int g_draw(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    if (!need_game(api, a[0], &gi)) return 1;
    for (int k = 0; k < g_nsprs; k++) {
        if (!g_sprs[k].alive || g_sprs[k].owner != gi) continue;
        RECT r = {(int)g_sprs[k].x, (int)g_sprs[k].y,
                  (int)(g_sprs[k].x + g_sprs[k].w), (int)(g_sprs[k].y + g_sprs[k].h)};
        HBRUSH b = brush_of(g_sprs[k].col);
        if (!b) return 1;
        FillRect(g_games[gi].back, &r, b);
        DeleteObject(b);
    }
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* ---------------- shared number slots (score, lives, speeds) ---------------- */

/* put # g, i, v — slot 0..15 */
static int g_put(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double i = 0, v = 0;
    if (!need_game(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &i) || !api->as_num(a[2], &v)) return 1;
    int idx = (int)i;
    if (idx < 0 || idx > 15) return 1;
    g_games[gi].slots[idx] = v;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* get # g, i — slot value */
static int g_get(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double i = 0;
    if (!need_game(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &i)) return 1;
    int idx = (int)i;
    if (idx < 0 || idx > 15) return 1;
    *out = ok_num(api, g_games[gi].slots[idx]);
    return *out ? 0 : 1;
}

/* ---------------- input ---------------- */

/* key # g, code — 1 while held (37-40 arrows, 32 space, 65-90 letters) */
static int g_key(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double c = 0;
    if (!need_game(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &c)) return 1;
    int code = (int)c;
    if (code < 0 || code > 255) return 1;
    pump();
    *out = ok_num(api, g_games[gi].alive && g_games[gi].held[code] ? 1 : 0);
    return *out ? 0 : 1;
}

/* pressed # g, code — 1 once per press */
static int g_pressed(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double c = 0;
    if (!need_game(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &c)) return 1;
    int code = (int)c;
    if (code < 0 || code > 255) return 1;
    pump();
    int hit = g_games[gi].alive && g_games[gi].once[code];
    g_games[gi].once[code] = 0;
    *out = ok_num(api, hit ? 1 : 0);
    return *out ? 0 : 1;
}

/* mouse # g — [x y] client vec */
static int g_mouse(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    if (!need_game(api, a[0], &gi)) return 1;
    pump();
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    VxOpVal *x = ok_num(api, (double)g_games[gi].mx);
    VxOpVal *y = ok_num(api, (double)g_games[gi].my);
    if (!x || !y || api->vec_push(v, x) != 0 || api->vec_push(v, y) != 0) {
        if (x) api->release(x);
        if (y) api->release(y);
        api->release(v);
        return 1;
    }
    api->release(x);
    api->release(y);
    *out = v;
    return 0;
}

/* mdown # g — 1 while left button held; mclick # g — 1 once per click */
static int g_mdown(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    if (!need_game(api, a[0], &gi)) return 1;
    pump();
    *out = ok_num(api, g_games[gi].alive && g_games[gi].mdown ? 1 : 0);
    return *out ? 0 : 1;
}

static int g_mclick(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    if (!need_game(api, a[0], &gi)) return 1;
    pump();
    int hit = g_games[gi].alive && g_games[gi].mclick;
    g_games[gi].mclick = 0;
    *out = ok_num(api, hit ? 1 : 0);
    return *out ? 0 : 1;
}

/* ---------------- pure helpers ---------------- */

/* hit # [x y w h], [x y w h] — rect overlap without sprites */
static int g_hit(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    size_t na = 0, nb = 0;
    if (!api->vec_len(a[0], &na) || na != 4) return 1;
    if (!api->vec_len(a[1], &nb) || nb != 4) return 1;
    double r[8];
    for (int k = 0; k < 4; k++) {
        VxOpVal *it = api->vec_get(a[0], (size_t)k);
        if (!it) return 1;
        int ok = api->as_num(it, &r[k]);
        api->release(it);
        if (!ok) return 1;
    }
    for (int k = 0; k < 4; k++) {
        VxOpVal *it = api->vec_get(a[1], (size_t)k);
        if (!it) return 1;
        int ok = api->as_num(it, &r[4 + k]);
        api->release(it);
        if (!ok) return 1;
    }
    *out = ok_num(api, rects_hit(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]));
    return *out ? 0 : 1;
}

/* dist # x1, y1, x2, y2 — pixels between dots */
static int g_dist(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x1, y1, x2, y2;
    if (!api->as_num(a[0], &x1) || !api->as_num(a[1], &y1) ||
        !api->as_num(a[2], &x2) || !api->as_num(a[3], &y2))
        return 1;
    double dx = x2 - x1, dy = y2 - y1;
    double d = sqrt(dx * dx + dy * dy);
    if (!isfinite(d)) return 1;
    *out = ok_num(api, d);
    return *out ? 0 : 1;
}

/* beep # freq, ms — tiny retro sound (keep ms short: it blocks) */
static int g_beep(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double f = 0, m = 0;
    if (!api->as_num(a[0], &f) || !api->as_num(a[1], &m)) return 1;
    if (f < 37) f = 37;
    if (f > 32767) f = 32767;
    if (m < 1) m = 1;
    if (m > 2000) m = 2000;
    if (!Beep((DWORD)f, (DWORD)m)) return 1;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "version", 0, g_version },
    { "make", 3, g_make },
    { "game", 3, g_game_alias },
    { "show", 1, g_show },
    { "open", 1, g_open },
    { "close", 1, g_close },
    { "run", 2, g_run },
    { "frame", 2, g_frame_loop },
    { "size", 1, g_size },
    { "dt", 1, g_dt },
    { "fps", 1, g_fps },
    { "tick", 1, g_tick },
    { "cls", 2, g_cls },
    { "fill", 2, g_fill },
    { "flip", 1, g_flip },
    { "rect", 6, g_rect },
    { "box", 6, g_box },
    { "circle", 5, g_circle },
    { "ring", 5, g_ring },
    { "line", 6, g_line },
    { "dot", 4, g_dot },
    { "px", 4, g_px },
    { "getpx", 3, g_getpx },
    { "text", 5, g_text },
    { "color", 3, g_color },
    { "spr", 6, g_spr },
    { "move", 3, g_move },
    { "place", 3, g_place },
    { "pos", 1, g_pos },
    { "spr_box", 1, g_spr_box },
    { "paint", 2, g_paint },
    { "kill", 1, g_kill },
    { "touch", 2, g_touch },
    { "draw", 1, g_draw },
    { "put", 3, g_put },
    { "get", 2, g_get },
    { "key", 2, g_key },
    { "pressed", 2, g_pressed },
    { "mouse", 1, g_mouse },
    { "mdown", 1, g_mdown },
    { "mclick", 1, g_mclick },
    { "hit", 2, g_hit },
    { "dist", 4, g_dist },
    { "beep", 2, g_beep },
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
    info.name = "VexGame";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
