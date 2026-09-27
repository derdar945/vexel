/* VexExec — run a command, catch merged output. Win32, UTF-8.
 *
 *   exec # "vexel.exe !vex_check main.vx", 10000  -> text (stdout+stderr)
 *   fail: cannot start, or the timeout (ms) runs out.
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

/* exec # cmd, timeout_ms */
static int f_exec(const VxOpApi *api, VxOpVal **a, int argc,
                  VxOpVal **out) {
    (void)argc;
    const char *cs = NULL;
    size_t cn = 0;
    double tmo = 0;
    if (!api->as_text(a[0], &cs, &cn) || !api->as_num(a[1], &tmo)) return 1;
    if (tmo < 100) tmo = 100;
    if (tmo > 600000) tmo = 600000;
    wchar_t *cmd = u8_to_w(cs, cn);
    if (!cmd) return 1;

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rout = NULL, wout = NULL;
    if (!CreatePipe(&rout, &wout, &sa, 0)) {
        free(cmd);
        return 1;
    }
    SetHandleInformation(rout, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wout;
    si.hStdError = wout;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    memset(&pi, 0, sizeof(pi));

    /* CreateProcessW may write into the command line: copy is ours. */
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmd);
    CloseHandle(wout);
    if (!ok) {
        CloseHandle(rout);
        return 1;
    }

    /* drain while waiting (avoids pipe deadlock on chatty children) */
    size_t cap = 65536, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        CloseHandle(rout);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }
    DWORD waited = 0;
    int done = 0, timed_out = 0;
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(rout, NULL, 0, NULL, &avail, NULL) && avail) {
            char tmp[8192];
            DWORD got = 0;
            DWORD want = avail > sizeof(tmp) ? (DWORD)sizeof(tmp) : avail;
            if (ReadFile(rout, tmp, want, &got, NULL) && got) {
                if (len + got + 1 > cap) {
                    size_t nc = cap * 2;
                    while (nc < len + got + 1) nc *= 2;
                    if (nc > 8 * 1024 * 1024) break; /* cap output */
                    char *nb = (char *)realloc(buf, nc);
                    if (!nb) break;
                    buf = nb;
                    cap = nc;
                }
                memcpy(buf + len, tmp, got);
                len += got;
            }
        }
        DWORD wr = WaitForSingleObject(pi.hProcess, 20);
        if (wr == WAIT_OBJECT_0) {
            done = 1;
            break;
        }
        waited += 20;
        if ((double)waited >= tmo) {
            timed_out = 1;
            break;
        }
    }
    /* last drain */
    if (!timed_out) {
        for (;;) {
            char tmp[8192];
            DWORD got = 0;
            if (!ReadFile(rout, tmp, sizeof(tmp), &got, NULL) || !got) break;
            if (len + got + 1 > cap) {
                size_t nc = cap * 2;
                while (nc < len + got + 1) nc *= 2;
                if (nc > 8 * 1024 * 1024) break;
                char *nb = (char *)realloc(buf, nc);
                if (!nb) break;
                buf = nb;
                cap = nc;
            }
            memcpy(buf + len, tmp, got);
            len += got;
        }
    }
    CloseHandle(rout);
    if (timed_out) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        free(buf);
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    (void)done;
    *out = api->make_text(buf, len);
    free(buf);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "exec", 2, f_exec },
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
    info.name = "VexExec";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
