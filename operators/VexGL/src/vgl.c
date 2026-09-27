/* VexGL — true 3D operator for Vexel. Win32 + WGL, fixed-function GL 1.1.
 *
 * Bench view (same loop idea as VexGame: operator owns the window and
 * the GL context, Vexel owns the step; frame state lives in slots,
 * the scene itself is stateless and redrawn every frame):
 *
 *   @ VexGL
 *   @ g : make # "GL cube", 640, 480
 *   & step :
 *     cls # g, 8, 10, 26
 *     cam # g, 0, 3, 8, 0, 0, 0
 *     cube # g, 0, 0, 0, 2, 30, (tick # g), 0, 16711680
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
#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct GLGame {
    int alive;
    HWND hwnd;
    HDC hdc;
    HGLRC hgl;
    int W, H;
    ULONGLONG last;
    double fps;
    int dt;
    long frame;
    BYTE held[256];
    BYTE once[256];
    double slots[16];
    int in_begin;
} GLGame;

static GLGame *g_gl = NULL;
static int g_ngl = 0, g_cgl = 0;
static int g_class_ok = 0;

static int new_gl(void) {
    for (int i = 0; i < g_ngl; i++) {
        if (!g_gl[i].alive) {
            memset(&g_gl[i], 0, sizeof(GLGame));
            return i;
        }
    }
    if (g_ngl + 1 > g_cgl) {
        int nc = g_cgl ? g_cgl * 2 : 8;
        GLGame *nd = (GLGame *)realloc(g_gl, (size_t)nc * sizeof(GLGame));
        if (!nd) return -1;
        g_gl = nd;
        g_cgl = nc;
    }
    memset(&g_gl[g_ngl], 0, sizeof(GLGame));
    return g_ngl++;
}

/* handle = index + 1 (0 never valid) */
static int gl_of(VxOpVal *h, const VxOpApi *api) {
    double d = 0;
    if (!api->as_num(h, &d)) return -1;
    long i = (long)d;
    if (i < 1 || i > g_ngl) return -1;
    if (!g_gl[i - 1].alive) return -1;
    return (int)(i - 1);
}

static void pump(void) {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static void cleanup_gl(int gi) {
    GLGame *g = &g_gl[gi];
    g->in_begin = 0;
    if (g->hgl) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g->hgl);
        g->hgl = NULL;
    }
    if (g->hdc) {
        if (g->hwnd) ReleaseDC(g->hwnd, g->hdc);
        g->hdc = NULL;
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    int gi = (int)(INT_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    GLGame *g = (gi >= 0 && gi < g_ngl) ? &g_gl[gi] : NULL;
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
        case WM_SIZE: {
            if (g && g->hgl) {
                int w = (int)(short)LOWORD(lp);
                int h = (int)(short)HIWORD(lp);
                if (w > 0 && h > 0) {
                    g->W = w;
                    g->H = h;
                    wglMakeCurrent(g->hdc, g->hgl);
                    glViewport(0, 0, w, h);
                }
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            if (g) {
                g->alive = 0;
                cleanup_gl(gi);
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
    kc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    kc.lpfnWndProc = wnd_proc;
    kc.hInstance = GetModuleHandleW(NULL);
    kc.lpszClassName = L"VexGLWindow";
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

static VxOpVal *ok_num(const VxOpApi *api, double d) {
    return api->make_num(d);
}

static int need_gl(const VxOpApi *api, VxOpVal *h, int *idx) {
    int i = gl_of(h, api);
    if (i < 0) return 0;
    *idx = i;
    return 1;
}

/* make the game's GL context current; 0 on dead/broken game */
static int gl_cur(const VxOpApi *api, VxOpVal *h, int *idx) {
    int i = 0;
    if (!need_gl(api, h, &i)) return 0;
    if (!g_gl[i].hwnd || !g_gl[i].hdc || !g_gl[i].hgl) return 0;
    if (!IsWindow(g_gl[i].hwnd)) return 0;
    if (!wglMakeCurrent(g_gl[i].hdc, g_gl[i].hgl)) return 0;
    *idx = i;
    return 1;
}

static GLubyte cl255(double d) {
    long l;
    if (!isfinite(d)) return 0;
    l = (long)d;
    if (l < 0) l = 0;
    if (l > 255) l = 255;
    return (GLubyte)l;
}

/* ---------------- camera math (no GLU on board) ---------------- */

static void mat_persp(float m[16], float fovy_deg, float aspect,
                      float zn, float zf) {
    float f = 1.0f / tanf(fovy_deg * 0.5f * 0.01745329252f);
    int k;
    for (k = 0; k < 16; k++) m[k] = 0.0f;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = 2.0f * zf * zn / (zn - zf);
}

static void mat_lookat(float m[16],
                       float ex, float ey, float ez,
                       float cx, float cy, float cz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    float sx, sy, sz, sl, ux, uy, uz;
    if (fl < 1e-6f) {
        fx = 0.0f; fy = 0.0f; fz = -1.0f; fl = 1.0f;
    }
    fx /= fl; fy /= fl; fz /= fl;
    /* s = normalize(f x up), up = (0,1,0) */
    sx = -fz; sy = 0.0f; sz = fx;
    sl = sqrtf(sx * sx + sz * sz);
    if (sl < 1e-6f) {
        sx = 1.0f; sy = 0.0f; sz = 0.0f;
    } else {
        sx /= sl; sz /= sl;
    }
    /* u = s x f */
    ux = sy * fz - sz * fy;
    uy = sz * fx - sx * fz;
    uz = sx * fy - sy * fx;
    m[0] = sx; m[1] = ux; m[2] = -fx; m[3] = 0.0f;
    m[4] = sy; m[5] = uy; m[6] = -fy; m[7] = 0.0f;
    m[8] = sz; m[9] = uz; m[10] = -fz; m[11] = 0.0f;
    m[12] = -(sx * ex + sy * ey + sz * ez);
    m[13] = -(ux * ex + uy * ey + uz * ez);
    m[14] = (fx * ex + fy * ey + fz * ez);
    m[15] = 1.0f;
}

static int apply_cam(int gi,
                     double ex, double ey, double ez,
                     double cx, double cy, double cz) {
    float p[16], v[16];
    float aspect;
    if (!isfinite(ex) || !isfinite(ey) || !isfinite(ez) ||
        !isfinite(cx) || !isfinite(cy) || !isfinite(cz))
        return 0;
    aspect = (g_gl[gi].H > 0) ? (float)g_gl[gi].W / (float)g_gl[gi].H : 1.0f;
    mat_persp(p, 60.0f, aspect, 0.1f, 200.0f);
    mat_lookat(v, (float)ex, (float)ey, (float)ez,
                  (float)cx, (float)cy, (float)cz);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(p);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(v);
    return 1;
}

/* ---------------- verbs ---------------- */

static int g_version(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_text("1.0.0", 5);
    return *out ? 0 : 1;
}

static int setup_gl_ctx(int gi) {
    GLGame *g = &g_gl[gi];
    PIXELFORMATDESCRIPTOR pfd;
    int pf;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    pf = ChoosePixelFormat(g->hdc, &pfd);
    if (pf == 0) return 0;
    if (!SetPixelFormat(g->hdc, pf, &pfd)) return 0;
    g->hgl = wglCreateContext(g->hdc);
    if (!g->hgl) return 0;
    if (!wglMakeCurrent(g->hdc, g->hgl)) {
        wglDeleteContext(g->hgl);
        g->hgl = NULL;
        return 0;
    }
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glViewport(0, 0, g->W, g->H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!apply_cam(gi, 0.0, 3.0, 8.0, 0.0, 0.0, 0.0)) return 0;
    return 1;
}

/* make # title, w, h — hidden GL window + context */
static int g_make(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *t = NULL;
    size_t tl = 0;
    double w = 0, h = 0;
    wchar_t *wt = NULL;
    RECT rc;
    HWND hwnd = NULL;
    int idx = -1;
    GLGame *g = NULL;
    if (!api->as_text(a[0], &t, &tl)) return 1;
    if (!api->as_num(a[1], &w) || !api->as_num(a[2], &h)) return 1;
    if (!ensure_class()) return 1;
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (w > 2048) w = 2048;
    if (h > 2048) h = 2048;
    wt = u8_to_w(t, tl);
    if (!wt) return 1;
    rc.left = 0; rc.top = 0; rc.right = (LONG)w; rc.bottom = (LONG)h;
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    hwnd = CreateWindowExW(0, L"VexGLWindow", wt,
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                           WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!hwnd) return 1;
    idx = new_gl();
    if (idx < 0) {
        DestroyWindow(hwnd);
        return 1;
    }
    g = &g_gl[idx];
    g->alive = 1;
    g->hwnd = hwnd;
    g->W = (int)w;
    g->H = (int)h;
    g->hdc = GetDC(hwnd);
    if (!g->hdc) {
        g->alive = 0;
        DestroyWindow(hwnd);
        return 1;
    }
    if (!setup_gl_ctx(idx)) {
        cleanup_gl(idx);
        g->alive = 0;
        DestroyWindow(hwnd);
        return 1;
    }
    g->last = 0;
    g->fps = 60.0;
    g->dt = 16;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)idx);
    *out = ok_num(api, (double)(idx + 1));
    return *out ? 0 : 1;
}

static int g_show(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_gl(api, a[0], &i)) return 1;
    ShowWindow(g_gl[i].hwnd, SW_SHOW);
    UpdateWindow(g_gl[i].hwnd);
    SetFocus(g_gl[i].hwnd);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int g_open(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_gl(api, a[0], &i)) {
        /* dead scene reads as closed, not as crash (like VexGame) */
        *out = ok_num(api, 0);
        return *out ? 0 : 1;
    }
    pump();
    *out = ok_num(api, g_gl[i].alive ? 1 : 0);
    return *out ? 0 : 1;
}

static int g_close(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_gl(api, a[0], &i)) return 1;
    g_gl[i].alive = 0;
    cleanup_gl(i);
    if (g_gl[i].hwnd) DestroyWindow(g_gl[i].hwnd);
    g_gl[i].hwnd = NULL;
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
        ULONGLONG now;
        ULONGLONG spent;
        VxOpVal *r = NULL;
        int keep;
        pump();
        if (!g_gl[gi].alive || !g_gl[gi].hwnd || !IsWindow(g_gl[gi].hwnd)) break;
        now = GetTickCount64();
        if (g_gl[gi].last == 0) {
            g_gl[gi].dt = 16;
        } else {
            ULONGLONG e = now - g_gl[gi].last;
            if (e > 250) e = 250;
            g_gl[gi].dt = (int)e;
            if (g_gl[gi].dt > 0) {
                double inst = 1000.0 / (double)g_gl[gi].dt;
                g_gl[gi].fps = g_gl[gi].fps * 0.9 + inst * 0.1;
            }
        }
        g_gl[gi].last = now;
        g_gl[gi].frame++;
        if (api->summon(step, NULL, 0, &r) != 0) {
            if (r) api->release(r);
            return 1;
        }
        keep = api->is_true(r);
        if (r) api->release(r);
        if (!keep) break;
        spent = GetTickCount64() - frame0;
        if (spent < 16) Sleep((DWORD)(16 - spent));
    }
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static int loop_arg(const VxOpApi *api, VxOpVal **a, int *gi, char *step) {
    const char *s = NULL;
    size_t sl = 0;
    if (!need_gl(api, a[0], gi)) return 0;
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
    VxOpVal *v = NULL;
    VxOpVal *w = NULL;
    VxOpVal *h = NULL;
    if (!need_gl(api, a[0], &i)) return 1;
    v = api->make_vec();
    if (!v) return 1;
    w = ok_num(api, (double)g_gl[i].W);
    h = ok_num(api, (double)g_gl[i].H);
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
    if (!need_gl(api, a[0], &i)) return 1;
    *out = ok_num(api, (double)g_gl[i].dt);
    return *out ? 0 : 1;
}

static int g_fps(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_gl(api, a[0], &i)) return 1;
    *out = ok_num(api, g_gl[i].fps);
    return *out ? 0 : 1;
}

/* tick # g — frames run so far */
static int g_tick(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_gl(api, a[0], &i)) return 1;
    pump();
    *out = ok_num(api, (double)g_gl[i].frame);
    return *out ? 0 : 1;
}

/* put # g, i, v — slot 0..15 */
static int g_put(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double i = 0, v = 0;
    int idx;
    if (!need_gl(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &i) || !api->as_num(a[2], &v)) return 1;
    idx = (int)i;
    if (idx < 0 || idx > 15) return 1;
    g_gl[gi].slots[idx] = v;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* get # g, i — slot value */
static int g_get(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double i = 0;
    int idx;
    if (!need_gl(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &i)) return 1;
    idx = (int)i;
    if (idx < 0 || idx > 15) return 1;
    *out = ok_num(api, g_gl[gi].slots[idx]);
    return *out ? 0 : 1;
}

/* key # g, code — 1 while held (37-40 arrows, 32 space, 65-90 letters) */
static int g_key(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double c = 0;
    int code;
    if (!need_gl(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &c)) return 1;
    code = (int)c;
    if (code < 0 || code > 255) return 1;
    pump();
    *out = ok_num(api, g_gl[gi].alive && g_gl[gi].held[code] ? 1 : 0);
    return *out ? 0 : 1;
}

/* pressed # g, code — 1 once per press */
static int g_pressed(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int gi = 0;
    double c = 0;
    int code;
    int hit;
    if (!need_gl(api, a[0], &gi)) return 1;
    if (!api->as_num(a[1], &c)) return 1;
    code = (int)c;
    if (code < 0 || code > 255) return 1;
    pump();
    hit = g_gl[gi].alive && g_gl[gi].once[code];
    g_gl[gi].once[code] = 0;
    *out = ok_num(api, hit ? 1 : 0);
    return *out ? 0 : 1;
}

/* ---------------- 3D scene ---------------- */

/* cls # g, r, g, b — clear color + depth (0-255 each) */
static int g_cls(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double r = 0, gg = 0, b = 0;
    if (!api->as_num(a[1], &r) || !api->as_num(a[2], &gg) ||
        !api->as_num(a[3], &b))
        return 1;
    if (!isfinite(r) || !isfinite(gg) || !isfinite(b)) return 1;
    if (!gl_cur(api, a[0], &i)) return 1;
    glClearColor(cl255(r) / 255.0f, cl255(gg) / 255.0f, cl255(b) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* cam # g, ex, ey, ez, cx, cy, cz — eye + look-at, up is +Y */
static int g_cam(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double e[6];
    int k;
    for (k = 0; k < 6; k++) {
        if (!api->as_num(a[1 + k], &e[k])) return 1;
    }
    if (!gl_cur(api, a[0], &i)) return 1;
    if (!apply_cam(i, e[0], e[1], e[2], e[3], e[4], e[5])) return 1;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static GLenum mode_of(int m) {
    if (m == 0) return GL_TRIANGLES;
    if (m == 1) return GL_QUADS;
    return GL_LINES;
}

/* begin # g, mode — open a mesh: 0 triangles, 1 quads, 2 lines */
static int g_begin(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double m = 0;
    if (!api->as_num(a[1], &m)) return 1;
    if (m != 0.0 && m != 1.0 && m != 2.0) return 1;
    if (!gl_cur(api, a[0], &i)) return 1;
    if (g_gl[i].in_begin) return 1;
    glBegin(mode_of((int)m));
    g_gl[i].in_begin = 1;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* col # g, r, g, b — current vertex color (0-255 each) */
static int g_col(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double r = 0, gg = 0, b = 0;
    if (!api->as_num(a[1], &r) || !api->as_num(a[2], &gg) ||
        !api->as_num(a[3], &b))
        return 1;
    if (!isfinite(r) || !isfinite(gg) || !isfinite(b)) return 1;
    if (!gl_cur(api, a[0], &i)) return 1;
    glColor3ub(cl255(r), cl255(gg), cl255(b));
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* v # g, x, y, z — one vertex */
static int g_v(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x = 0, y = 0, z = 0;
    if (!api->as_num(a[1], &x) || !api->as_num(a[2], &y) ||
        !api->as_num(a[3], &z))
        return 1;
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) return 1;
    if (!gl_cur(api, a[0], &i)) return 1;
    if (!g_gl[i].in_begin) return 1;
    glVertex3f((float)x, (float)y, (float)z);
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* end # g — close the mesh */
static int g_end(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!gl_cur(api, a[0], &i)) return 1;
    if (!g_gl[i].in_begin) return 1;
    glEnd();
    g_gl[i].in_begin = 0;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* cube faces: unit cube corners, per-face shade for fake lighting */
static void cube_face(double r, double gg, double b, double shade,
                      double ax, double ay, double az,
                      double bx, double by, double bz,
                      double cx, double cy, double cz,
                      double dx, double dy, double dz) {
    double f = shade;
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    glColor3ub(cl255(r * f), cl255(gg * f), cl255(b * f));
    glVertex3d(ax, ay, az);
    glVertex3d(bx, by, bz);
    glVertex3d(cx, cy, cz);
    glVertex3d(dx, dy, dz);
}

/* cube # g, x, y, z, size, rx, ry, rz, color — solid shaded cube.
 * Rotations in degrees; color is one 0xRRGGBB number. */
static int g_cube(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double p[8];
    int k;
    double x, y, z, s, r, gg, b;
    for (k = 0; k < 8; k++) {
        if (!api->as_num(a[1 + k], &p[k])) return 1;
        if (!isfinite(p[k])) return 1;
    }
    if (!gl_cur(api, a[0], &i)) return 1;
    if (g_gl[i].in_begin) return 1;
    x = p[0]; y = p[1]; z = p[2]; s = p[3];
    if (s <= 0.0) return 1;
    r = (double)(((unsigned long)(long)p[7] >> 16) & 0xFFUL);
    gg = (double)(((unsigned long)(long)p[7] >> 8) & 0xFFUL);
    b = (double)((unsigned long)(long)p[7] & 0xFFUL);
    glPushMatrix();
    glTranslated(x, y, z);
    glRotated(p[4], 1.0, 0.0, 0.0);
    glRotated(p[5], 0.0, 1.0, 0.0);
    glRotated(p[6], 0.0, 0.0, 1.0);
    glScaled(s * 0.5, s * 0.5, s * 0.5);
    glBegin(GL_QUADS);
    /* top +Y */
    cube_face(r, gg, b, 1.00,
              -1, 1, -1, 1, 1, -1, 1, 1, 1, -1, 1, 1);
    /* bottom -Y */
    cube_face(r, gg, b, 0.35,
              -1, -1, -1, -1, -1, 1, 1, -1, 1, 1, -1, -1);
    /* front +Z */
    cube_face(r, gg, b, 0.75,
              -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1);
    /* back -Z */
    cube_face(r, gg, b, 0.50,
              1, -1, -1, -1, -1, -1, -1, 1, -1, 1, 1, -1);
    /* right +X */
    cube_face(r, gg, b, 0.85,
              1, -1, -1, 1, -1, 1, 1, 1, 1, 1, 1, -1);
    /* left -X */
    cube_face(r, gg, b, 0.60,
              -1, -1, 1, -1, -1, -1, -1, 1, -1, -1, 1, 1);
    glEnd();
    glPopMatrix();
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* flip # g — swap buffers (call once at the end of step) */
static int g_flip(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_gl(api, a[0], &i)) return 1;
    if (!g_gl[i].hwnd || !g_gl[i].hdc) return 1;
    if (!SwapBuffers(g_gl[i].hdc)) return 1;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "version", 0, g_version },
    { "make", 3, g_make },
    { "show", 1, g_show },
    { "open", 1, g_open },
    { "close", 1, g_close },
    { "run", 2, g_run },
    { "frame", 2, g_frame_loop },
    { "size", 1, g_size },
    { "dt", 1, g_dt },
    { "fps", 1, g_fps },
    { "tick", 1, g_tick },
    { "put", 3, g_put },
    { "get", 2, g_get },
    { "key", 2, g_key },
    { "pressed", 2, g_pressed },
    { "cls", 4, g_cls },
    { "cam", 7, g_cam },
    { "begin", 2, g_begin },
    { "col", 4, g_col },
    { "v", 4, g_v },
    { "end", 1, g_end },
    { "cube", 9, g_cube },
    { "flip", 1, g_flip },
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
    info.name = "VexGL";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
