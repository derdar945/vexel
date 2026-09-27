/* VexFS — native file operator for Vexel. Win32, UTF-8 paths. */
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

static int api_path(const VxOpApi *api, VxOpVal *v, wchar_t **out) {
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(v, &s, &n)) return 0;
    *out = u8_to_w(s, n);
    return *out ? 1 : 0;
}

/* read # path — whole file as text */
static int f_read(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    wchar_t *p = NULL;
    if (!api_path(api, a[0], &p)) return 1;
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(p);
    if (h == INVALID_HANDLE_VALUE) return 1;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(h);
        return 1;
    }
    size_t n = (size_t)sz.QuadPart;
    char *buf = (char *)malloc(n ? n : 1);
    if (!buf) {
        CloseHandle(h);
        return 1;
    }
    DWORD got = 0, off = 0;
    while (off < n) {
        DWORD chunk = 0;
        if (!ReadFile(h, buf + off, (DWORD)(n - off), &chunk, NULL) || chunk == 0) break;
        off += chunk;
    }
    (void)got;
    CloseHandle(h);
    /* strip UTF-8 BOM */
    size_t start = (off >= 3 && (unsigned char)buf[0] == 0xEF &&
                    (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;
    *out = api->make_text(buf + start, off - start);
    free(buf);
    return *out ? 0 : 1;
}

/* write # path, text — whole file, 1 ok */
static int f_write(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    wchar_t *p = NULL;
    const char *s = NULL;
    size_t n = 0;
    if (!api_path(api, a[0], &p) || !api->as_text(a[1], &s, &n)) {
        free(p);
        return 1;
    }
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    free(p);
    if (h == INVALID_HANDLE_VALUE) return 1;
    size_t off = 0;
    while (off < n) {
        DWORD chunk = 0;
        size_t want = n - off > 0x7FFFFFFF ? 0x7FFFFFFF : n - off;
        if (!WriteFile(h, s + off, (DWORD)want, &chunk, NULL) || chunk == 0) {
            CloseHandle(h);
            return 1;
        }
        off += chunk;
    }
    CloseHandle(h);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* dir # path — vec of names (no . / ..), fail if bad */
static int f_dir(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    wchar_t *p = NULL;
    if (!api_path(api, a[0], &p)) return 1;
    size_t L = wcslen(p);
    wchar_t *pat = (wchar_t *)malloc((L + 4) * sizeof(wchar_t));
    if (!pat) {
        free(p);
        return 1;
    }
    wcscpy(pat, p);
    if (L && pat[L - 1] != L'\\' && pat[L - 1] != L'/') {
        pat[L] = L'\\';
        pat[L + 1] = 0;
    }
    wcscat(pat, L"*");
    free(p);
    VxOpVal *v = api->make_vec();
    if (!v) {
        free(pat);
        return 1;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    free(pat);
    if (h == INVALID_HANDLE_VALUE) {
        api->release(v);
        return 1;
    }
    int rc = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        char *u = w_to_u8(fd.cFileName);
        if (!u) {
            rc = 1;
            break;
        }
        VxOpVal *item = api->make_text(u, strlen(u));
        free(u);
        if (!item || api->vec_push(v, item) != 0) {
            if (item) api->release(item);
            rc = 1;
            break;
        }
        api->release(item);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (rc) {
        api->release(v);
        return 1;
    }
    *out = v;
    return 0;
}

/* exists # path — 1/0 */
static int f_exists(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    wchar_t *p = NULL;
    if (!api_path(api, a[0], &p)) return 1;
    DWORD at = GetFileAttributesW(p);
    free(p);
    *out = api->make_num(at == INVALID_FILE_ATTRIBUTES ? 0 : 1);
    return *out ? 0 : 1;
}

/* remove # path — 1 ok, fail otherwise */
static int f_remove(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    wchar_t *p = NULL;
    if (!api_path(api, a[0], &p)) return 1;
    BOOL ok = DeleteFileW(p);
    free(p);
    if (!ok) return 1;
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "read", 1, f_read },
    { "write", 2, f_write },
    { "dir", 1, f_dir },
    { "exists", 1, f_exists },
    { "remove", 1, f_remove },
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
    info.name = "VexFS";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
