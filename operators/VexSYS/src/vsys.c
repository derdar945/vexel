/* VexSYS 2.0.0 — MEGA system operator for Vexel. Win32, UTF-8.
 *
 *   Python-like battery: os + sys + time + path + env + process
 *   + random/uuid + strings + math, in VexFS style:
 *     @ VexSYS
 *     > os #
 *     > abspath # "note.txt"
 *     > split # "a,b,c", ","
 */
#define _CRT_SECURE_NO_WARNINGS
#include "operator.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <wctype.h>

/* RtlGenRandom (SystemFunction036) lives in advapi32 — no new link needed. */
BOOLEAN __stdcall SystemFunction036(PVOID RandomBuffer, ULONG RandomBufferLength);

/* ---------------- UTF-8 <-> wide helpers ---------------- */

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

static void ensure_rand_seeded(void) {
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned)(time(NULL) ^ (time_t)GetCurrentProcessId() ^ (time_t)GetTickCount()));
    }
}

static int sys_random_bytes(unsigned char *buf, size_t n) {
    if (n == 0) return 1;
    if (SystemFunction036(buf, (ULONG)n)) return 1;
    /* fallback: stdlib rand */
    ensure_rand_seeded();
    for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)(rand() & 0xFF);
    return 1;
}

/* Windows abs check on UTF-8 bytes (ASCII slashes / drive). */
static int path_is_abs_u8(const char *s, size_t n) {
    if (n == 0) return 0;
    if (s[0] == '/' || s[0] == '\\') return 1;
    if (n >= 3 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
        s[1] == ':' && (s[2] == '/' || s[2] == '\\'))
        return 1;
    return 0;
}

static long long filetime_to_unix(FILETIME ft) {
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    if (li.QuadPart < 116444736000000000ULL) return 0;
    return (long long)((li.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

/* ---------------- core identity ---------------- */

static int s_version(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_text("2.0.0", 5);
    return *out ? 0 : 1;
}

static int s_os(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_text("windows", 7);
    return *out ? 0 : 1;
}

static int s_arch(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    const char *s = "any";
#if defined(_M_X64) || defined(__x86_64__)
    s = "x64";
#elif defined(_M_IX86) || defined(__i386__)
    s = "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
    s = "arm64";
#endif
    *out = api->make_text(s, strlen(s));
    return *out ? 0 : 1;
}

/* os_ver # — "major.minor.build", e.g. "10.0.22631" */
static int s_os_ver(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    char buf[64] = {0};
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (nt) {
        typedef LONG(__stdcall *RGV)(LPOSVERSIONINFOW);
        RGV f = (RGV)(void *)GetProcAddress(nt, "RtlGetVersion");
        if (f) {
            OSVERSIONINFOW vi;
            memset(&vi, 0, sizeof(vi));
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (f(&vi) == 0) {
                snprintf(buf, sizeof(buf), "%lu.%lu.%lu",
                         (unsigned long)vi.dwMajorVersion,
                         (unsigned long)vi.dwMinorVersion,
                         (unsigned long)vi.dwBuildNumber);
            }
        }
    }
    if (!buf[0]) snprintf(buf, sizeof(buf), "windows");
    *out = api->make_text(buf, strlen(buf));
    return *out ? 0 : 1;
}

/* cpu # — logical processors */
static int s_cpu(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    *out = api->make_num((double)si.dwNumberOfProcessors);
    return *out ? 0 : 1;
}

/* mem # — vec [total_mb, free_mb] */
static int s_mem(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    VxOpVal *t = api->make_num((double)(ms.ullTotalPhys / (1024.0 * 1024.0)));
    VxOpVal *f = t ? api->make_num((double)(ms.ullAvailPhys / (1024.0 * 1024.0))) : NULL;
    if (!t || !f || api->vec_push(v, t) != 0 || api->vec_push(v, f) != 0) {
        if (t) api->release(t);
        if (f) api->release(f);
        api->release(v);
        return 1;
    }
    api->release(t);
    api->release(f);
    *out = v;
    return 0;
}

/* uptime # — seconds since boot */
static int s_uptime(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_num((double)(GetTickCount64() / 1000ULL));
    return *out ? 0 : 1;
}

/* tick # — ms since boot */
static int s_tick(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_num((double)GetTickCount64());
    return *out ? 0 : 1;
}

static int s_user(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    wchar_t buf[257];
    DWORD n = 257;
    if (!GetUserNameW(buf, &n) || n == 0) return 1;
    char *u = w_to_u8_n(buf, n - 1);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

static int s_hostname(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buf, &n)) return 1;
    char *u = w_to_u8_n(buf, n);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

static int s_cwd(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    DWORD n = GetCurrentDirectoryW(0, NULL);
    if (n == 0) return 1;
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
    if (!w) return 1;
    if (!GetCurrentDirectoryW(n + 1, w)) { free(w); return 1; }
    char *u = w_to_u8(w);
    free(w);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

/* chdir # path — 1 ok */
static int s_chdir(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    BOOL ok = SetCurrentDirectoryW(w);
    free(w);
    if (!ok) return 1;
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* mkdir # path — 1 ok (already exists counts as ok) */
static int s_mkdir(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    if (!CreateDirectoryW(w, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        free(w);
        return 1;
    }
    free(w);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* exe # — current executable path */
static int s_exe(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    DWORD cap = 512;
    wchar_t *w = NULL;
    for (;;) {
        wchar_t *nb = (wchar_t *)realloc(w, (size_t)cap * sizeof(wchar_t));
        if (!nb) { free(w); return 1; }
        w = nb;
        DWORD got = GetModuleFileNameW(NULL, w, cap);
        if (got == 0) { free(w); return 1; }
        if (got < cap) break;
        cap *= 2;
        if (cap > 32768) { free(w); return 1; }
    }
    char *u = w_to_u8(w);
    free(w);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

static int s_pid(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_num((double)GetCurrentProcessId());
    return *out ? 0 : 1;
}

static int s_args(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    int narg = 0;
    LPWSTR *wv = CommandLineToArgvW(GetCommandLineW(), &narg);
    if (!wv) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) { LocalFree(wv); return 1; }
    for (int i = 0; i < narg; i++) {
        char *u = w_to_u8(wv[i]);
        if (!u) { api->release(v); LocalFree(wv); return 1; }
        VxOpVal *item = api->make_text(u, strlen(u));
        free(u);
        if (!item) { api->release(v); LocalFree(wv); return 1; }
        if (api->vec_push(v, item) != 0) {
            api->release(item); api->release(v); LocalFree(wv); return 1;
        }
        api->release(item);
    }
    LocalFree(wv);
    *out = v;
    return 0;
}

/* exit # code — never returns */
static int s_exit(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)api; (void)argc; (void)out;
    double c = 0;
    if (!api->as_num(a[0], &c)) return 1;
    if (c < 0) c = 0;
    if (c > 255) c = 255;
    ExitProcess((UINT)(int)c);
    return 1;
}

/* ---------------- time ---------------- */

static int s_time(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_num((double)time(NULL));
    return *out ? 0 : 1;
}

static int s_time_ms(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime; li.HighPart = ft.dwHighDateTime;
    double ms = 0;
    if (li.QuadPart >= 116444736000000000ULL)
        ms = (double)((li.QuadPart - 116444736000000000ULL) / 10000ULL);
    *out = api->make_num(ms);
    return *out ? 0 : 1;
}

static int s_date(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    time_t t = time(NULL);
    struct tm lt;
#if defined(_WIN32)
    if (localtime_s(&lt, &t) != 0) return 1;
#else
    lt = *localtime(&t);
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             lt.tm_hour, lt.tm_min, lt.tm_sec);
    *out = api->make_text(buf, strlen(buf));
    return *out ? 0 : 1;
}

static int s_date_utc(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    SYSTEMTIME st;
    GetSystemTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             (int)st.wYear, (int)st.wMonth, (int)st.wDay,
             (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    *out = api->make_text(buf, strlen(buf));
    return *out ? 0 : 1;
}

static int s_sleep(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double ms = 0;
    if (!api->as_num(a[0], &ms)) return 1;
    if (ms < 0) ms = 0;
    if (ms > 600000) ms = 600000;
    Sleep((DWORD)ms);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* ---------------- env ---------------- */

static int s_env(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *wname = u8_to_w(s, n);
    if (!wname) return 1;
    DWORD need = GetEnvironmentVariableW(wname, NULL, 0);
    if (need == 0) { free(wname); return 1; }
    wchar_t *wval = (wchar_t *)malloc((size_t)(need + 1) * sizeof(wchar_t));
    if (!wval) { free(wname); return 1; }
    DWORD got = GetEnvironmentVariableW(wname, wval, need + 1);
    free(wname);
    if (got >= need + 1) { free(wval); return 1; }
    char *u = w_to_u8(wval);
    free(wval);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

static int s_has_env(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *wname = u8_to_w(s, n);
    if (!wname) return 1;
    DWORD need = GetEnvironmentVariableW(wname, NULL, 0);
    free(wname);
    *out = api->make_num(need == 0 ? 0 : 1);
    return *out ? 0 : 1;
}

static int s_set_env(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s0 = NULL, *s1 = NULL; size_t n0 = 0, n1 = 0;
    if (!api->as_text(a[0], &s0, &n0)) return 1;
    if (!api->as_text(a[1], &s1, &n1)) return 1;
    if (n0 == 0) return 1;
    wchar_t *wname = u8_to_w(s0, n0);
    wchar_t *wval = u8_to_w(s1, n1);
    if (!wname || !wval) { free(wname); free(wval); return 1; }
    BOOL ok = SetEnvironmentVariableW(wname, wval);
    free(wname); free(wval);
    if (!ok) return 1;
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

static int s_unset_env(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    if (n == 0) return 1;
    wchar_t *wname = u8_to_w(s, n);
    if (!wname) return 1;
    BOOL ok = SetEnvironmentVariableW(wname, NULL);
    free(wname);
    if (!ok) return 1;
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

/* env_all # — vec of "K=V" (UTF-8) */
static int s_env_all(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    LPWCH block = GetEnvironmentStringsW();
    if (!block) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) { FreeEnvironmentStringsW(block); return 1; }
    for (LPWCH p = block; *p; ) {
        char *u = w_to_u8(p);
        if (!u) { api->release(v); FreeEnvironmentStringsW(block); return 1; }
        VxOpVal *item = api->make_text(u, strlen(u));
        free(u);
        if (!item) { api->release(v); FreeEnvironmentStringsW(block); return 1; }
        if (api->vec_push(v, item) != 0) {
            api->release(item); api->release(v);
            FreeEnvironmentStringsW(block); return 1;
        }
        api->release(item);
        p += wcslen(p) + 1;
    }
    FreeEnvironmentStringsW(block);
    *out = v;
    return 0;
}

/* ---------------- path ---------------- */

/* join # a, b — path join; abs b wins */
static int s_join(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s0 = NULL, *s1 = NULL; size_t n0 = 0, n1 = 0;
    if (!api->as_text(a[0], &s0, &n0)) return 1;
    if (!api->as_text(a[1], &s1, &n1)) return 1;
    if (path_is_abs_u8(s1, n1) || n0 == 0) {
        *out = api->make_text(s1, n1);
        return *out ? 0 : 1;
    }
    if (n1 == 0) {
        *out = api->make_text(s0, n0);
        return *out ? 0 : 1;
    }
    size_t e = n0;
    while (e > 0 && (s0[e - 1] == '\\' || s0[e - 1] == '/')) e--;
    size_t need = e + 1 + n1;
    char *buf = (char *)malloc(need + 1);
    if (!buf) return 1;
    memcpy(buf, s0, e);
    buf[e] = '\\';
    memcpy(buf + e + 1, s1, n1);
    buf[need] = 0;
    *out = api->make_text(buf, need);
    free(buf);
    return *out ? 0 : 1;
}

/* base # p — file name after last slash */
static int s_base(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    size_t last = n;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\') last = i + 1;
    *out = api->make_text(s + last, n - last);
    return *out ? 0 : 1;
}

/* dir_name # p — before last slash, "" when none */
static int s_dir_name(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    size_t last = n + 1;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\') last = i;
    if (last == n + 1) {
        *out = api->make_text("", 0);
        return *out ? 0 : 1;
    }
    if (last == 0) {
        *out = api->make_text(s, 1);
        return *out ? 0 : 1;
    }
    *out = api->make_text(s, last);
    return *out ? 0 : 1;
}

/* ext # p — ".ext" or "" */
static int s_ext(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    size_t slash = n;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\') slash = i;
    size_t dot = n;
    for (size_t i = (slash == n ? 0 : slash + 1); i < n; i++)
        if (s[i] == '.') dot = i;
    if (dot == n) {
        *out = api->make_text("", 0);
        return *out ? 0 : 1;
    }
    size_t bstart = (slash == n ? 0 : slash + 1);
    if (dot == bstart) { /* ".gitignore" -> no ext */
        *out = api->make_text("", 0);
        return *out ? 0 : 1;
    }
    *out = api->make_text(s + dot, n - dot);
    return *out ? 0 : 1;
}

/* stem # p — base without ext */
static int s_stem(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    size_t slash = n;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\') slash = i;
    size_t bstart = (slash == n ? 0 : slash + 1);
    size_t dot = n;
    for (size_t i = bstart; i < n; i++)
        if (s[i] == '.') dot = i;
    if (dot == n || dot == bstart) {
        *out = api->make_text(s + bstart, n - bstart);
        return *out ? 0 : 1;
    }
    *out = api->make_text(s + bstart, dot - bstart);
    return *out ? 0 : 1;
}

/* abspath # p — full path */
static int s_abspath(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    DWORD need = GetFullPathNameW(w, 0, NULL, NULL);
    free(w);
    if (need == 0) return 1;
    wchar_t *wfull = (wchar_t *)malloc((size_t)(need + 1) * sizeof(wchar_t));
    if (!wfull) return 1;
    wchar_t *ww = u8_to_w(s, n);
    if (!ww) { free(wfull); return 1; }
    DWORD got = GetFullPathNameW(ww, need + 1, wfull, NULL);
    free(ww);
    if (got == 0 || got > need) { free(wfull); return 1; }
    char *u = w_to_u8(wfull);
    free(wfull);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

static int s_is_abs(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    *out = api->make_num(path_is_abs_u8(s, n) ? 1 : 0);
    return *out ? 0 : 1;
}

static int s_is_file(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    DWORD at = GetFileAttributesW(w);
    free(w);
    int ok = (at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
    *out = api->make_num(ok);
    return *out ? 0 : 1;
}

static int s_is_dir(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    DWORD at = GetFileAttributesW(w);
    free(w);
    int ok = (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
    *out = api->make_num(ok);
    return *out ? 0 : 1;
}

/* size # path — bytes, fail for dirs/missing */
static int s_size(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    WIN32_FILE_ATTRIBUTE_DATA fd;
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &fd)) { free(w); return 1; }
    free(w);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 1;
    ULARGE_INTEGER li;
    li.LowPart = fd.nFileSizeLow; li.HighPart = fd.nFileSizeHigh;
    *out = api->make_num((double)li.QuadPart);
    return *out ? 0 : 1;
}

/* mtime # path — unix seconds */
static int s_mtime(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    WIN32_FILE_ATTRIBUTE_DATA fd;
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &fd)) { free(w); return 1; }
    free(w);
    *out = api->make_num((double)filetime_to_unix(fd.ftLastWriteTime));
    return *out ? 0 : 1;
}

/* which # name — full path via PATH, fail when missing */
static int s_which(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    if (n == 0) return 1;
    int has_slash = 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\') { has_slash = 1; break; }
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    wchar_t full[MAX_PATH * 2];
    wchar_t *found = NULL;
    if (has_slash) {
        DWORD at = GetFileAttributesW(w);
        if (at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY)) {
            DWORD need = GetFullPathNameW(w, 0, NULL, NULL);
            if (need && need < (DWORD)(sizeof(full) / sizeof(full[0]))) {
                if (GetFullPathNameW(w, (DWORD)(sizeof(full) / sizeof(full[0])), full, NULL)) {
                    char *u = w_to_u8(full);
                    free(w);
                    if (!u) return 1;
                    *out = api->make_text(u, strlen(u));
                    free(u);
                    return *out ? 0 : 1;
                }
            }
        }
        free(w);
        return 1;
    }
    DWORD r = SearchPathW(NULL, w, L".exe", (DWORD)(sizeof(full) / sizeof(full[0])), full, &found);
    if (r == 0 || r >= (DWORD)(sizeof(full) / sizeof(full[0]))) {
        r = SearchPathW(NULL, w, NULL, (DWORD)(sizeof(full) / sizeof(full[0])), full, &found);
    }
    free(w);
    if (r == 0 || r >= (DWORD)(sizeof(full) / sizeof(full[0]))) return 1;
    char *u = w_to_u8(full);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

static int s_path_sep(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    *out = api->make_text("\\", 1);
    return *out ? 0 : 1;
}

/* ---------------- random ---------------- */

static int s_rand(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double mx = 0;
    if (!api->as_num(a[0], &mx)) return 1;
    int m = (int)mx;
    if (m <= 0) return 1;
    ensure_rand_seeded();
    int r = (int)((double)rand() / ((double)RAND_MAX + 1.0) * (double)m);
    if (r < 0) r = 0;
    if (r >= m) r = m - 1;
    *out = api->make_num((double)r);
    return *out ? 0 : 1;
}

static int s_rand_range(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double dlo = 0, dhi = 0;
    if (!api->as_num(a[0], &dlo)) return 1;
    if (!api->as_num(a[1], &dhi)) return 1;
    int lo = (int)dlo, hi = (int)dhi;
    if (hi <= lo) return 1;
    ensure_rand_seeded();
    int span = hi - lo;
    int r = (int)((double)rand() / ((double)RAND_MAX + 1.0) * (double)span);
    *out = api->make_num((double)(lo + r));
    return *out ? 0 : 1;
}

/* rand_bytes # n — vec of random bytes */
static int s_rand_bytes(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double dn = 0;
    if (!api->as_num(a[0], &dn)) return 1;
    int n = (int)dn;
    if (n <= 0 || n > 4096) return 1;
    unsigned char *buf = (unsigned char *)malloc((size_t)n);
    if (!buf) return 1;
    sys_random_bytes(buf, (size_t)n);
    VxOpVal *v = api->make_vec();
    if (!v) { free(buf); return 1; }
    for (int i = 0; i < n; i++) {
        VxOpVal *it = api->make_num((double)buf[i]);
        if (!it) { free(buf); api->release(v); return 1; }
        if (api->vec_push(v, it) != 0) {
            api->release(it); free(buf); api->release(v); return 1;
        }
        api->release(it);
    }
    free(buf);
    *out = v;
    return 0;
}

/* choice # vec — random element */
static int s_choice(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    size_t n = 0;
    if (!api->vec_len(a[0], &n) || n == 0) return 1;
    ensure_rand_seeded();
    size_t idx = (size_t)((double)rand() / ((double)RAND_MAX + 1.0) * (double)n);
    if (idx >= n) idx = n - 1;
    VxOpVal *it = api->vec_get(a[0], idx);
    if (!it) return 1;
    *out = it;
    return 0;
}

/* shuffle # vec — shuffled copy */
static int s_shuffle(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    size_t n = 0;
    if (!api->vec_len(a[0], &n)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    if (n == 0) { *out = v; return 0; }
    if (n > 100000) { api->release(v); return 1; }
    VxOpVal **arr = (VxOpVal **)malloc(n * sizeof(VxOpVal *));
    if (!arr) { api->release(v); return 1; }
    for (size_t i = 0; i < n; i++) {
        arr[i] = api->vec_get(a[0], i);
        if (!arr[i]) {
            for (size_t k = 0; k < i; k++) api->release(arr[k]);
            free(arr); api->release(v); return 1;
        }
    }
    ensure_rand_seeded();
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)((double)rand() / ((double)RAND_MAX + 1.0) * (double)(i + 1));
        if (j > i) j = i;
        VxOpVal *t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
    for (size_t i = 0; i < n; i++) {
        if (api->vec_push(v, arr[i]) != 0) {
            for (size_t k = 0; k < n; k++) api->release(arr[k]);
            free(arr); api->release(v); return 1;
        }
    }
    for (size_t i = 0; i < n; i++) api->release(arr[i]);
    free(arr);
    *out = v;
    return 0;
}

/* uuid # — v4 GUID text */
static int s_uuid(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)a; (void)argc;
    unsigned char b[16];
    sys_random_bytes(b, sizeof(b));
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
    char buf[40];
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    *out = api->make_text(buf, 36);
    return *out ? 0 : 1;
}

/* ---------------- strings ---------------- */

static int s_upper(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    for (wchar_t *p = w; *p; p++) *p = towupper(*p);
    char *u = w_to_u8(w);
    free(w);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

static int s_lower(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    for (wchar_t *p = w; *p; p++) *p = towlower(*p);
    char *u = w_to_u8(w);
    free(w);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

static int s_trim(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    wchar_t *w = u8_to_w(s, n);
    if (!w) return 1;
    size_t len = wcslen(w), st = 0, en = len;
    while (st < en && iswspace(w[st])) st++;
    while (en > st && iswspace(w[en - 1])) en--;
    char *u = w_to_u8_n(w + st, (DWORD)(en - st));
    free(w);
    if (!u) return 1;
    *out = api->make_text(u, strlen(u));
    free(u);
    return *out ? 0 : 1;
}

/* split # s, sep — vec (sep must be non-empty) */
static int s_split(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL, *d = NULL; size_t n = 0, m = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    if (!api->as_text(a[1], &d, &m)) return 1;
    if (m == 0) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    if (n == 0) {
        VxOpVal *it = api->make_text("", 0);
        if (!it || api->vec_push(v, it) != 0) {
            if (it) api->release(it);
            api->release(v); return 1;
        }
        api->release(it);
        *out = v;
        return 0;
    }
    size_t start = 0;
    for (size_t i = 0; i + m <= n; ) {
        if (memcmp(s + i, d, m) == 0) {
            VxOpVal *it = api->make_text(s + start, i - start);
            if (!it || api->vec_push(v, it) != 0) {
                if (it) api->release(it);
                api->release(v); return 1;
            }
            api->release(it);
            i += m;
            start = i;
        } else {
            i++;
        }
    }
    VxOpVal *tail = api->make_text(s + start, n - start);
    if (!tail || api->vec_push(v, tail) != 0) {
        if (tail) api->release(tail);
        api->release(v); return 1;
    }
    api->release(tail);
    *out = v;
    return 0;
}

/* lines # s — vec split on \r\n / \n / \r */
static int s_lines(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL; size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    VxOpVal *v = api->make_vec();
    if (!v) return 1;
    size_t start = 0;
    for (size_t i = 0; i < n; ) {
        if (s[i] == '\r' || s[i] == '\n') {
            VxOpVal *it = api->make_text(s + start, i - start);
            if (!it || api->vec_push(v, it) != 0) {
                if (it) api->release(it);
                api->release(v); return 1;
            }
            api->release(it);
            if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') i += 2;
            else i += 1;
            start = i;
        } else {
            i++;
        }
    }
    VxOpVal *tail = api->make_text(s + start, n - start);
    if (!tail || api->vec_push(v, tail) != 0) {
        if (tail) api->release(tail);
        api->release(v); return 1;
    }
    api->release(tail);
    *out = v;
    return 0;
}

/* str_join # sep, vec — text */
static int s_str_join(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *sep = NULL; size_t seplen = 0;
    if (!api->as_text(a[0], &sep, &seplen)) return 1;
    size_t n = 0;
    if (!api->vec_len(a[1], &n)) return 1;
    size_t cap = 64, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return 1;
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && seplen) {
            while (len + seplen + 1 > cap) {
                cap *= 2;
                char *nb = (char *)realloc(buf, cap);
                if (!nb) { free(buf); return 1; }
                buf = nb;
            }
            memcpy(buf + len, sep, seplen);
            len += seplen;
        }
        VxOpVal *it = api->vec_get(a[1], i);
        if (!it) { free(buf); return 1; }
        const char *t = NULL; size_t tl = 0;
        if (!api->as_text(it, &t, &tl)) { api->release(it); free(buf); return 1; }
        while (len + tl + 1 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { api->release(it); free(buf); return 1; }
            buf = nb;
        }
        if (tl) memcpy(buf + len, t, tl);
        len += tl;
        api->release(it);
    }
    *out = api->make_text(buf, len);
    free(buf);
    return *out ? 0 : 1;
}

static int s_starts(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL, *p = NULL; size_t n = 0, m = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    if (!api->as_text(a[1], &p, &m)) return 1;
    int ok = (m <= n && memcmp(s, p, m) == 0) ? 1 : 0;
    *out = api->make_num(ok);
    return *out ? 0 : 1;
}

static int s_ends(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL, *p = NULL; size_t n = 0, m = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    if (!api->as_text(a[1], &p, &m)) return 1;
    int ok = (m <= n && memcmp(s + n - m, p, m) == 0) ? 1 : 0;
    *out = api->make_num(ok);
    return *out ? 0 : 1;
}

static int s_contains(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL, *p = NULL; size_t n = 0, m = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    if (!api->as_text(a[1], &p, &m)) return 1;
    int ok = 0;
    if (m == 0) ok = 1;
    else if (m <= n) {
        for (size_t i = 0; i + m <= n; i++) {
            if (memcmp(s + i, p, m) == 0) { ok = 1; break; }
        }
    }
    *out = api->make_num(ok);
    return *out ? 0 : 1;
}

/* replace # s, old, new — all occurrences, old non-empty */
static int s_replace(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL, *o = NULL, *nw = NULL;
    size_t n = 0, no = 0, nn = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    if (!api->as_text(a[1], &o, &no)) return 1;
    if (!api->as_text(a[2], &nw, &nn)) return 1;
    if (no == 0) return 1;
    size_t cap = n + 1, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return 1;
    for (size_t i = 0; i < n; ) {
        int hit = (i + no <= n && memcmp(s + i, o, no) == 0);
        const char *chunk = hit ? nw : s + i;
        size_t cl = hit ? nn : 1;
        while (len + cl + 1 > cap) {
            cap = cap * 2 + cl;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return 1; }
            buf = nb;
        }
        if (cl) memcpy(buf + len, chunk, cl);
        len += cl;
        i += hit ? no : 1;
    }
    *out = api->make_text(buf, len);
    free(buf);
    return *out ? 0 : 1;
}

/* ---------------- math ---------------- */

static int s_abs(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0;
    if (!api->as_num(a[0], &x)) return 1;
    *out = api->make_num(fabs(x));
    return *out ? 0 : 1;
}

static int s_floor(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0;
    if (!api->as_num(a[0], &x)) return 1;
    *out = api->make_num(floor(x));
    return *out ? 0 : 1;
}

static int s_ceil(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0;
    if (!api->as_num(a[0], &x)) return 1;
    *out = api->make_num(ceil(x));
    return *out ? 0 : 1;
}

static int s_sqrt(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0;
    if (!api->as_num(a[0], &x)) return 1;
    if (x < 0) return 1;
    double r = sqrt(x);
    if (!isfinite(r)) return 1;
    *out = api->make_num(r);
    return *out ? 0 : 1;
}

static int s_pow(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0;
    if (!api->as_num(a[0], &x)) return 1;
    if (!api->as_num(a[1], &y)) return 1;
    double r = pow(x, y);
    if (!isfinite(r)) return 1;
    *out = api->make_num(r);
    return *out ? 0 : 1;
}

static int s_min(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0;
    if (!api->as_num(a[0], &x)) return 1;
    if (!api->as_num(a[1], &y)) return 1;
    *out = api->make_num(x < y ? x : y);
    return *out ? 0 : 1;
}

static int s_max(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0, y = 0;
    if (!api->as_num(a[0], &x)) return 1;
    if (!api->as_num(a[1], &y)) return 1;
    *out = api->make_num(x > y ? x : y);
    return *out ? 0 : 1;
}

static int s_clamp(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    double x = 0, lo = 0, hi = 0;
    if (!api->as_num(a[0], &x)) return 1;
    if (!api->as_num(a[1], &lo)) return 1;
    if (!api->as_num(a[2], &hi)) return 1;
    if (hi < lo) return 1;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    *out = api->make_num(x);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "version", 0, s_version },
    { "os", 0, s_os },
    { "arch", 0, s_arch },
    { "os_ver", 0, s_os_ver },
    { "cpu", 0, s_cpu },
    { "mem", 0, s_mem },
    { "uptime", 0, s_uptime },
    { "tick", 0, s_tick },
    { "user", 0, s_user },
    { "hostname", 0, s_hostname },
    { "cwd", 0, s_cwd },
    { "chdir", 1, s_chdir },
    { "mkdir", 1, s_mkdir },
    { "exe", 0, s_exe },
    { "pid", 0, s_pid },
    { "args", 0, s_args },
    { "exit", 1, s_exit },
    { "time", 0, s_time },
    { "time_ms", 0, s_time_ms },
    { "date", 0, s_date },
    { "date_utc", 0, s_date_utc },
    { "sleep", 1, s_sleep },
    { "env", 1, s_env },
    { "has_env", 1, s_has_env },
    { "set_env", 2, s_set_env },
    { "unset_env", 1, s_unset_env },
    { "env_all", 0, s_env_all },
    { "join", 2, s_join },
    { "base", 1, s_base },
    { "dir_name", 1, s_dir_name },
    { "ext", 1, s_ext },
    { "stem", 1, s_stem },
    { "abspath", 1, s_abspath },
    { "is_abs", 1, s_is_abs },
    { "is_file", 1, s_is_file },
    { "is_dir", 1, s_is_dir },
    { "size", 1, s_size },
    { "mtime", 1, s_mtime },
    { "which", 1, s_which },
    { "path_sep", 0, s_path_sep },
    { "rand", 1, s_rand },
    { "rand_range", 2, s_rand_range },
    { "rand_bytes", 1, s_rand_bytes },
    { "choice", 1, s_choice },
    { "shuffle", 1, s_shuffle },
    { "uuid", 0, s_uuid },
    { "upper", 1, s_upper },
    { "lower", 1, s_lower },
    { "trim", 1, s_trim },
    { "split", 2, s_split },
    { "lines", 1, s_lines },
    { "str_join", 2, s_str_join },
    { "starts", 2, s_starts },
    { "ends", 2, s_ends },
    { "contains", 2, s_contains },
    { "replace", 3, s_replace },
    { "abs", 1, s_abs },
    { "floor", 1, s_floor },
    { "ceil", 1, s_ceil },
    { "sqrt", 1, s_sqrt },
    { "pow", 2, s_pow },
    { "min", 2, s_min },
    { "max", 2, s_max },
    { "clamp", 3, s_clamp },
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
    info.name = "VexSYS";
    info.version = "2.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
