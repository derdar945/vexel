/* Vex3D — scenes, objects, software rasterizer. Win32, no libs.
 *
 *   @ Vex3D
 *   @ s : scene # 320, 200, "cube"
 *   @ m : cube #
 *   @ o : spawn # s, m, 0, 0, 5
 *   & step :
 *     turn # o, tick # s, tick # s * 2, 0
 *     = open # s
 *   .
 *   > frame # s, "step"
 *
 * Pipeline per object: euler rotate (X then Y then Z, degrees),
 * scale, translate; camera sits at cam pos looking +Z (no cam
 * rotation in 1.0.0); perspective project; flat shade by face
 * normal; z-buffered barycentric fill into a DIB.
 */
#include "operator.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef PI
#define PI 3.141592653589793
#endif

typedef struct Tri {
    float v[3][3];
} Tri;

typedef struct Mesh {
    int alive;
    Tri *tris;
    int ntris;
} Mesh;

typedef struct Obj {
    int alive;
    int scene;   /* index into g_scenes */
    int mesh;    /* index into g_meshes */
    float x, y, z;
    float rx, ry, rz;
    float scale;
    unsigned color; /* 0xRRGGBB */
} Obj;

typedef struct Scene {
    int alive;
    HWND hwnd;
    int w, h;
    HBITMAP bmp;
    unsigned *px; /* w*h, 0x00RRGGBB */
    float *zb;
    float cx, cy, cz; /* camera pos */
    float fov;        /* degrees */
    long tick;
} Scene;

static Mesh *g_meshes = NULL;
static int g_nmeshes = 0, g_capmeshes = 0;
static Obj *g_objs = NULL;
static int g_nobjs = 0, g_capobjs = 0;
static Scene *g_scenes = NULL;
static int g_nscenes = 0, g_capscenes = 0;
static int g_class_ok = 0;

static int new_mesh(void) {
    for (int i = 0; i < g_nmeshes; i++) {
        if (!g_meshes[i].alive) {
            free(g_meshes[i].tris);
            memset(&g_meshes[i], 0, sizeof(Mesh));
            return i;
        }
    }
    if (g_nmeshes + 1 > g_capmeshes) {
        int nc = g_capmeshes ? g_capmeshes * 2 : 8;
        Mesh *nd = (Mesh *)realloc(g_meshes, (size_t)nc * sizeof(Mesh));
        if (!nd) return -1;
        g_meshes = nd;
        g_capmeshes = nc;
    }
    memset(&g_meshes[g_nmeshes], 0, sizeof(Mesh));
    return g_nmeshes++;
}

static int new_obj(void) {
    for (int i = 0; i < g_nobjs; i++) {
        if (!g_objs[i].alive) {
            memset(&g_objs[i], 0, sizeof(Obj));
            return i;
        }
    }
    if (g_nobjs + 1 > g_capobjs) {
        int nc = g_capobjs ? g_capobjs * 2 : 16;
        Obj *nd = (Obj *)realloc(g_objs, (size_t)nc * sizeof(Obj));
        if (!nd) return -1;
        g_objs = nd;
        g_capobjs = nc;
    }
    memset(&g_objs[g_nobjs], 0, sizeof(Obj));
    return g_nobjs++;
}

static int new_scene(void) {
    for (int i = 0; i < g_nscenes; i++) {
        if (!g_scenes[i].alive) {
            memset(&g_scenes[i], 0, sizeof(Scene));
            return i;
        }
    }
    if (g_nscenes + 1 > g_capscenes) {
        int nc = g_capscenes ? g_capscenes * 2 : 8;
        Scene *nd = (Scene *)realloc(g_scenes, (size_t)nc * sizeof(Scene));
        if (!nd) return -1;
        g_scenes = nd;
        g_capscenes = nc;
    }
    memset(&g_scenes[g_nscenes], 0, sizeof(Scene));
    return g_nscenes++;
}

static void free_game(Scene *g) {
    if (g->bmp) {
        DeleteObject(g->bmp);
        g->bmp = NULL;
    }
    free(g->zb);
    g->zb = NULL;
    g->px = NULL;
    g->alive = 0;
    g->hwnd = NULL;
}

static int mesh_of(const VxOpApi *api, VxOpVal *h) {
    double d = 0;
    if (!api->as_num(h, &d)) return -1;
    long i = (long)d;
    if (i < 1 || i > g_nmeshes || !g_meshes[i - 1].alive) return -1;
    return (int)(i - 1);
}

static int obj_of(const VxOpApi *api, VxOpVal *h) {
    double d = 0;
    if (!api->as_num(h, &d)) return -1;
    long i = (long)d;
    if (i < 1 || i > g_nobjs || !g_objs[i - 1].alive) return -1;
    return (int)(i - 1);
}

static int scene_of(const VxOpApi *api, VxOpVal *h) {
    double d = 0;
    if (!api->as_num(h, &d)) return -1;
    long i = (long)d;
    if (i < 1 || i > g_nscenes || !g_scenes[i - 1].alive) return -1;
    return (int)(i - 1);
}

static void pump(void) {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static int find_scene(HWND hwnd) {
    for (int i = 0; i < g_nscenes; i++) {
        if (g_scenes[i].alive && g_scenes[i].hwnd == hwnd) return i;
    }
    return -1;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        int gi = find_scene(hwnd);
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (gi >= 0 && g_scenes[gi].bmp) {
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, g_scenes[gi].bmp);
            BitBlt(dc, 0, 0, g_scenes[gi].w, g_scenes[gi].h, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        int gi = find_scene(hwnd);
        if (gi >= 0) free_game(&g_scenes[gi]);
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
    kc.lpszClassName = L"Vex3DWindow";
    kc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    kc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
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

static int api_num(const VxOpApi *api, VxOpVal *v, double *d) {
    return api->as_num(v, d);
}

static VxOpVal *ok_num(const VxOpApi *api, double d) {
    return api->make_num(d);
}

/* scene # w, h, title */
static int f_scene(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double w = 0, h = 0;
    const char *t = NULL;
    size_t tl = 0;
    if (!api_num(api, a[0], &w) || !api_num(api, a[1], &h) ||
        !api->as_text(a[2], &t, &tl))
        return 1;
    int wi = (int)w, hi = (int)h;
    if (wi < 16) wi = 16;
    if (hi < 16) hi = 16;
    if (wi > 1280) wi = 1280;
    if (hi > 800) hi = 800;
    if (!ensure_class()) return 1;
    wchar_t *wt = u8_to_w(t, tl);
    if (!wt) return 1;
    RECT r = { 0, 0, wi, hi };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
    HWND hwnd = CreateWindowExW(0, L"Vex3DWindow", wt,
                                (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME) | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top, NULL, NULL,
                                GetModuleHandleW(NULL), NULL);
    free(wt);
    if (!hwnd) return 1;
    int idx = new_scene();
    if (idx < 0) {
        DestroyWindow(hwnd);
        return 1;
    }
    Scene *g = &g_scenes[idx];
    g->alive = 1;
    g->hwnd = hwnd;
    g->w = wi;
    g->h = hi;
    g->cx = 0;
    g->cy = 0;
    g->cz = 0;
    g->fov = 70;
    g->tick = 0;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = wi;
    bi.bmiHeader.biHeight = -hi;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HDC dc = GetDC(hwnd);
    g->bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(hwnd, dc);
    g->zb = (float *)malloc((size_t)wi * (size_t)hi * sizeof(float));
    if (!g->bmp || !bits || !g->zb) {
        if (g->bmp) DeleteObject(g->bmp);
        free(g->zb);
        g->alive = 0;
        DestroyWindow(hwnd);
        return 1;
    }
    g->px = (unsigned *)bits;
    memset(bits, 0, (size_t)wi * (size_t)hi * 4);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    *out = ok_num(api, (double)(idx + 1));
    return *out ? 0 : 1;
}

static int need_scene(const VxOpApi *api, VxOpVal *h, int *idx) {
    int i = scene_of(api, h);
    if (i < 0) return 0;
    *idx = i;
    return 1;
}

/* mesh # flatvec — 9 numbers per triangle */
static int f_mesh(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    size_t n = 0;
    if (!api->vec_len(a[0], &n) || n == 0 || n % 9 != 0) return 1;
    if (n > 9 * 4096) return 1;
    int idx = new_mesh();
    if (idx < 0) return 1;
    Mesh *m = &g_meshes[idx];
    int nt = (int)(n / 9);
    m->tris = (Tri *)malloc((size_t)nt * sizeof(Tri));
    if (!m->tris) return 1;
    m->ntris = nt;
    for (int t = 0; t < nt; t++) {
        for (int v = 0; v < 3; v++) {
            for (int k = 0; k < 3; k++) {
                VxOpVal *e = api->vec_get(a[0], (size_t)(t * 9 + v * 3 + k));
                if (!e) {
                    free(m->tris);
                    m->tris = NULL;
                    m->ntris = 0;
                    return 1;
                }
                double d = 0;
                int ok = api->as_num(e, &d);
                api->release(e);
                if (!ok) {
                    free(m->tris);
                    m->tris = NULL;
                    m->ntris = 0;
                    return 1;
                }
                m->tris[t].v[v][k] = (float)d;
            }
        }
    }
    m->alive = 1;
    *out = ok_num(api, (double)(idx + 1));
    return *out ? 0 : 1;
}

/* cube # — unit cube, 12 triangles */
static int f_cube(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a;
    (void)argc;
    static const float c[12][3][3] = {
        { { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } },
        { { -1, -1, -1 }, { 1, 1, -1 }, { 1, -1, -1 } },
        { { -1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
        { { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 } },
        { { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 } },
        { { -1, -1, -1 }, { -1, 1, 1 }, { -1, 1, -1 } },
        { { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } },
        { { 1, -1, -1 }, { 1, 1, 1 }, { 1, -1, 1 } },
        { { -1, 1, -1 }, { -1, 1, 1 }, { 1, 1, 1 } },
        { { -1, 1, -1 }, { 1, 1, 1 }, { 1, 1, -1 } },
        { { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 } },
        { { -1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } },
    };
    int idx = new_mesh();
    if (idx < 0) return 1;
    Mesh *m = &g_meshes[idx];
    m->tris = (Tri *)malloc(12 * sizeof(Tri));
    if (!m->tris) return 1;
    m->ntris = 12;
    for (int t = 0; t < 12; t++)
        for (int v = 0; v < 3; v++)
            for (int k = 0; k < 3; k++) m->tris[t].v[v][k] = c[t][v][k];
    m->alive = 1;
    *out = ok_num(api, (double)(idx + 1));
    return *out ? 0 : 1;
}

/* spawn # scene, mesh, x, y, z */
static int f_spawn(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int si = 0, mi = 0;
    double x = 0, y = 0, z = 0;
    if (!need_scene(api, a[0], &si)) return 1;
    if (mesh_of(api, a[1]) < 0) return 1;
    mi = mesh_of(api, a[1]);
    if (!api_num(api, a[2], &x) || !api_num(api, a[3], &y) ||
        !api_num(api, a[4], &z))
        return 1;
    int idx = new_obj();
    if (idx < 0) return 1;
    Obj *o = &g_objs[idx];
    o->alive = 1;
    o->scene = si;
    o->mesh = mi;
    o->x = (float)x;
    o->y = (float)y;
    o->z = (float)z;
    o->rx = 0;
    o->ry = 0;
    o->rz = 0;
    o->scale = 1;
    o->color = 0xFFFFFF;
    *out = ok_num(api, (double)(idx + 1));
    return *out ? 0 : 1;
}

static int need_obj(const VxOpApi *api, VxOpVal *h, int *idx) {
    int i = obj_of(api, h);
    if (i < 0) return 0;
    *idx = i;
    return 1;
}

/* kill # obj */
static int f_kill(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_obj(api, a[0], &i)) return 1;
    g_objs[i].alive = 0;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* move # obj, x, y, z */
static int f_move(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x = 0, y = 0, z = 0;
    if (!need_obj(api, a[0], &i) || !api_num(api, a[1], &x) ||
        !api_num(api, a[2], &y) || !api_num(api, a[3], &z))
        return 1;
    g_objs[i].x = (float)x;
    g_objs[i].y = (float)y;
    g_objs[i].z = (float)z;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* turn # obj, rx, ry, rz (degrees) */
static int f_turn(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x = 0, y = 0, z = 0;
    if (!need_obj(api, a[0], &i) || !api_num(api, a[1], &x) ||
        !api_num(api, a[2], &y) || !api_num(api, a[3], &z))
        return 1;
    g_objs[i].rx = (float)x;
    g_objs[i].ry = (float)y;
    g_objs[i].rz = (float)z;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* scale # obj, s */
static int f_scale(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double s = 0;
    if (!need_obj(api, a[0], &i) || !api_num(api, a[1], &s)) return 1;
    if (s <= 0) s = 1;
    g_objs[i].scale = (float)s;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* color # obj, rgb */
static int f_color(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double c = 0;
    if (!need_obj(api, a[0], &i) || !api_num(api, a[1], &c)) return 1;
    g_objs[i].color = ((unsigned)(long)c) & 0xFFFFFF;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* cam # scene, x, y, z, fov */
static int f_cam(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x = 0, y = 0, z = 0, f = 0;
    if (!need_scene(api, a[0], &i) || !api_num(api, a[1], &x) ||
        !api_num(api, a[2], &y) || !api_num(api, a[3], &z) ||
        !api_num(api, a[4], &f))
        return 1;
    if (f < 10) f = 10;
    if (f > 160) f = 160;
    g_scenes[i].cx = (float)x;
    g_scenes[i].cy = (float)y;
    g_scenes[i].cz = (float)z;
    g_scenes[i].fov = (float)f;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* ---- rasterizer ---- */

static void rot_point(float *p, float rx, float ry, float rz) {
    float ax = rx * (float)(PI / 180.0);
    float ay = ry * (float)(PI / 180.0);
    float az = rz * (float)(PI / 180.0);
    float cx = cosf(ax), sx = sinf(ax);
    float cy = cosf(ay), sy = sinf(ay);
    float cz = cosf(az), sz = sinf(az);
    float x = p[0], y = p[1], z = p[2];
    float y1 = y * cx - z * sx, z1 = y * sx + z * cx;
    float x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy;
    p[0] = x2 * cz - y1 * sz;
    p[1] = x2 * sz + y1 * cz;
    p[2] = z2;
}

static void fill_tri(Scene *g, float ax, float ay, float az, float bx, float by,
                     float bz, float cx, float cy, float cz, unsigned color) {
    float minx = ax, maxx = ax, miny = ay, maxy = ay;
    if (bx < minx) minx = bx;
    if (bx > maxx) maxx = bx;
    if (cx < minx) minx = cx;
    if (cx > maxx) maxx = cx;
    if (by < miny) miny = by;
    if (by > maxy) maxy = by;
    if (cy < miny) miny = cy;
    if (cy > maxy) maxy = cy;
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;
    float d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (d > -1e-6f && d < 1e-6f) return;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            float l1 = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / d;
            float l2 = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / d;
            float l3 = 1.0f - l1 - l2;
            if (l1 < 0 || l2 < 0 || l3 < 0) continue;
            float z = l1 * az + l2 * bz + l3 * cz;
            size_t k = (size_t)y * (size_t)g->w + (size_t)x;
            if (z < g->zb[k]) {
                g->zb[k] = z;
                g->px[k] = color;
            }
        }
    }
}

static unsigned shade(unsigned color, float glow) {
    if (glow < 0) glow = 0;
    if (glow > 1) glow = 1;
    unsigned r = (color >> 16) & 0xFF;
    unsigned g = (color >> 8) & 0xFF;
    unsigned b = color & 0xFF;
    r = (unsigned)(r * glow);
    g = (unsigned)(g * glow);
    b = (unsigned)(b * glow);
    return (r << 16) | (g << 8) | b;
}

static void render(Scene *g) {
    size_t n = (size_t)g->w * (size_t)g->h;
    for (size_t k = 0; k < n; k++) {
        g->px[k] = 0;
        g->zb[k] = 1e30f;
    }
    float focal = ((float)g->h * 0.5f) / tanf(g->fov * 0.5f * (float)(PI / 180.0));
    float cx = (float)g->w * 0.5f, cy = (float)g->h * 0.5f;
    for (int oi = 0; oi < g_nobjs; oi++) {
        Obj *o = &g_objs[oi];
        if (!o->alive) continue;
        /* which scene? objects store scene index */
        if (o->scene < 0 || o->scene >= g_nscenes) continue;
        if (&g_scenes[o->scene] != g) continue;
        if (o->mesh < 0 || o->mesh >= g_nmeshes) continue;
        Mesh *m = &g_meshes[o->mesh];
        if (!m->alive || !m->tris) continue;
        for (int t = 0; t < m->ntris; t++) {
            float p[3][3];
            for (int v = 0; v < 3; v++) {
                p[v][0] = m->tris[t].v[v][0] * o->scale;
                p[v][1] = m->tris[t].v[v][1] * o->scale;
                p[v][2] = m->tris[t].v[v][2] * o->scale;
                rot_point(p[v], o->rx, o->ry, o->rz);
                p[v][0] += o->x - g->cx;
                p[v][1] += o->y - g->cy;
                p[v][2] += o->z - g->cz;
            }
            /* face normal (view space) for shading + cull */
            float ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1], uz = p[1][2] - p[0][2];
            float vx = p[2][0] - p[0][0], vy = p[2][1] - p[0][1], vz = p[2][2] - p[0][2];
            float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nl < 1e-9f) continue;
            nx /= nl;
            ny /= nl;
            nz /= nl;
            if (nz >= -0.02f) continue; /* backface: camera looks +Z */
            /* project */
            float sx[3], sy[3], sz[3];
            int behind = 0;
            for (int v = 0; v < 3; v++) {
                if (p[v][2] < 0.1f) {
                    behind = 1;
                    break;
                }
                sx[v] = cx + focal * p[v][0] / p[v][2];
                sy[v] = cy - focal * p[v][1] / p[v][2];
                sz[v] = p[v][2];
            }
            if (behind) continue;
            float glow = 0.35f + 0.65f * (0.5f * (-nx) + 0.5f * (-ny) + 0.7f * (-nz));
            fill_tri(g, sx[0], sy[0], sz[0], sx[1], sy[1], sz[1], sx[2], sy[2], sz[2],
                     shade(o->color, glow));
        }
    }
    InvalidateRect(g->hwnd, NULL, FALSE);
}

/* draw # s — render one frame now (tests, thumbnails) */
static int f_draw(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_scene(api, a[0], &i)) return 1;
    Scene *g = &g_scenes[i];
    if (!g->alive) return 1;
    pump();
    render(g);
    g->tick++;
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* close # s */
static int f_close(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_scene(api, a[0], &i)) return 1;
    free_game(&g_scenes[i]);
    DestroyWindow(g_scenes[i].hwnd);
    pump();
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* open # s — 1 alive else 0 */
static int f_open(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_scene(api, a[0], &i)) {
        *out = ok_num(api, 0);
        return *out ? 0 : 1;
    }
    pump();
    *out = ok_num(api, g_scenes[i].alive ? 1 : 0);
    return *out ? 0 : 1;
}

/* frame # s, "step" — render, tick++, summon step; dry lands */
static int f_frame(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_scene(api, a[0], &i)) return 1;
    const char *s = NULL;
    size_t sl = 0;
    if (!api->as_text(a[1], &s, &sl) || sl == 0 || sl > 48) return 1;
    char step[64];
    memcpy(step, s, sl);
    step[sl] = '\0';
    if (!api->summon) return 1;
    Scene *g = &g_scenes[i];
    for (;;) {
        pump();
        if (!g->alive || !IsWindow(g->hwnd)) break;
        render(g);
        VxOpVal *r = NULL;
        if (api->summon(step, NULL, 0, &r) != 0) {
            if (r) api->release(r);
            return 1;
        }
        int keep = api->is_true(r);
        if (r) api->release(r);
        g->tick++;
        if (!keep) break;
        Sleep(8);
    }
    *out = ok_num(api, 1);
    return *out ? 0 : 1;
}

/* tick # s */
static int f_tick(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_scene(api, a[0], &i)) return 1;
    *out = ok_num(api, (double)g_scenes[i].tick);
    return *out ? 0 : 1;
}

/* getpx # s, x, y */
static int f_getpx(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double x = 0, y = 0;
    if (!need_scene(api, a[0], &i) || !api_num(api, a[1], &x) ||
        !api_num(api, a[2], &y))
        return 1;
    Scene *g = &g_scenes[i];
    long xi = (long)x, yi = (long)y;
    if (xi < 0 || yi < 0 || xi >= g->w || yi >= g->h || !g->px) return 1;
    *out = ok_num(api, (double)(g->px[(size_t)yi * (size_t)g->w + (size_t)xi]));
    return *out ? 0 : 1;
}

/* key # s, code */
static int f_key(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    double c = 0;
    if (!need_scene(api, a[0], &i) || !api_num(api, a[1], &c)) return 1;
    int code = (int)c;
    if (code < 0 || code > 255) return 1;
    pump();
    SHORT st = GetAsyncKeyState(code);
    *out = ok_num(api, (st & 0x8000) ? 1 : 0);
    return *out ? 0 : 1;
}

/* size # s */
static int f_size(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    int i = 0;
    if (!need_scene(api, a[0], &i)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    VxOpVal *w = api->make_num((double)g_scenes[i].w);
    VxOpVal *h = api->make_num((double)g_scenes[i].h);
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

static const VxOpFuncInfo g_funcs[] = {
    { "scene", 3, f_scene },
    { "draw", 1, f_draw },
    { "close", 1, f_close },
    { "open", 1, f_open },
    { "frame", 2, f_frame },
    { "tick", 1, f_tick },
    { "mesh", 1, f_mesh },
    { "cube", 0, f_cube },
    { "spawn", 5, f_spawn },
    { "kill", 1, f_kill },
    { "move", 4, f_move },
    { "turn", 4, f_turn },
    { "scale", 2, f_scale },
    { "color", 2, f_color },
    { "cam", 5, f_cam },
    { "getpx", 3, f_getpx },
    { "key", 2, f_key },
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
    info.name = "Vex3D";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}