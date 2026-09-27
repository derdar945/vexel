/* VexNet — plain HTTP for Vexel. Winsock, no TLS (documented).
 *
 *   get # "http://example.com/"          -> body text
 *   serve # 8080, "step"                 -> loop until step runs dry
 *   & step req :
 *     = "hi " + req
 *   .
 */
#include "operator.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int g_ws = 0;

static int ws_up(void) {
    if (g_ws) return 1;
    WSADATA d;
    memset(&d, 0, sizeof(d));
    if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return 0;
    g_ws = 1;
    return 1;
}

/* read until peer closes or cap; returns malloc'd buffer + length */
static int read_all(SOCKET s, char **buf, size_t *len) {
    size_t cap = 65536, n = 0;
    char *b = (char *)malloc(cap);
    if (!b) return 0;
    for (;;) {
        if (n + 8192 + 1 > cap) {
            if (cap >= 8 * 1024 * 1024) break;
            size_t nc = cap * 2;
            char *nb = (char *)realloc(b, nc);
            if (!nb) break;
            b = nb;
            cap = nc;
        }
        int got = recv(s, b + n, 8192, 0);
        if (got <= 0) break;
        n += (size_t)got;
    }
    b[n] = '\0';
    *buf = b;
    *len = n;
    return 1;
}

/* split "http://host[:port][/path]" — malloc'd host/path, port out */
static int split_url(const char *u, char **host, int *port, char **path) {
    if (strncmp(u, "http://", 7) != 0) return 0;
    u += 7;
    const char *slash = strchr(u, '/');
    size_t hlen = slash ? (size_t)(slash - u) : strlen(u);
    char *h = (char *)malloc(hlen + 1);
    if (!h) return 0;
    memcpy(h, u, hlen);
    h[hlen] = '\0';
    int p = 80;
    char *colon = strchr(h, ':');
    if (colon) {
        *colon = '\0';
        p = atoi(colon + 1);
        if (p <= 0 || p > 65535) {
            free(h);
            return 0;
        }
    }
    if (!*h) {
        free(h);
        return 0;
    }
    char *pa = _strdup(slash ? slash : "/");
    if (!pa) {
        free(h);
        return 0;
    }
    *host = h;
    *port = p;
    *path = pa;
    return 1;
}

static SOCKET dial(const char *host, int port) {
    struct addrinfo hints, *res = NULL, *rp;
    char ps[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(ps, sizeof(ps), "%d", port);
    if (getaddrinfo(host, ps, &hints, &res) != 0) return INVALID_SOCKET;
    SOCKET s = INVALID_SOCKET;
    for (rp = res; rp; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        DWORD tmo = 10000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof(tmo));
        if (connect(s, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return s;
}

/* get # url — body of a plain-http GET */
static int f_get(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *u = NULL;
    size_t un = 0;
    if (!api->as_text(a[0], &u, &un)) return 1;
    char *url = (char *)malloc(un + 1);
    if (!url) return 1;
    memcpy(url, u, un);
    url[un] = '\0';
    char *host = NULL, *path = NULL;
    int port = 80;
    if (!ws_up() || !split_url(url, &host, &port, &path)) {
        free(url);
        return 1;
    }
    free(url);
    SOCKET s = dial(host, port);
    if (s == INVALID_SOCKET) {
        free(host);
        free(path);
        return 1;
    }
    char req[4096];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n"
             "User-Agent: VexNet/1.0\r\n\r\n",
             path, host);
    free(host);
    free(path);
    size_t rl = strlen(req), sent = 0;
    while (sent < rl) {
        int n = send(s, req + sent, (int)(rl - sent), 0);
        if (n <= 0) {
            closesocket(s);
            return 1;
        }
        sent += (size_t)n;
    }
    char *buf = NULL;
    size_t len = 0;
    if (!read_all(s, &buf, &len)) {
        closesocket(s);
        return 1;
    }
    closesocket(s);
    /* strip headers; a silent close (no bytes at all) is fail */
    char *body = strstr(buf, "\r\n\r\n");
    size_t off = 0;
    if (body) {
        off = (size_t)(body + 4 - buf);
    } else if (len == 0) {
        free(buf);
        return 1;
    }
    *out = api->make_text(buf + off, len - off);
    free(buf);
    return *out ? 0 : 1;
}

/* read one request: until blank line or 64KB */
static int read_request(SOCKET s, char **buf, size_t *len) {
    size_t cap = 8192, n = 0;
    char *b = (char *)malloc(cap);
    if (!b) return 0;
    for (;;) {
        if (n + 2048 > cap) {
            if (cap >= 65536) break;
            size_t nc = cap * 2;
            char *nb = (char *)realloc(b, nc);
            if (!nb) break;
            b = nb;
            cap = nc;
        }
        int got = recv(s, b + n, 1024, 0);
        if (got <= 0) break;
        n += (size_t)got;
        b[n] = '\0';
        if (strstr(b, "\r\n\r\n")) break;
    }
    *buf = b;
    *len = n;
    return n > 0;
}

static void send_all(SOCKET s, const char *b, size_t n) {
    size_t off = 0;
    while (off < n) {
        int k = send(s, b + off, (int)(n - off > 65536 ? 65536 : n - off), 0);
        if (k <= 0) break;
        off += (size_t)k;
    }
}

/* serve # port, "step" — step takes raw request text, answers body.
 * Dry answer lands the server. */
static int f_serve(const VxOpApi *api, VxOpVal **a, int argc,
                   VxOpVal **out) {
    (void)argc;
    double pd = 0;
    const char *s = NULL;
    size_t sl = 0;
    if (!api->as_num(a[0], &pd) || !api->as_text(a[1], &s, &sl)) return 1;
    int port = (int)pd;
    if (port <= 0 || port > 65535 || sl == 0 || sl > 48) return 1;
    char step[64];
    memcpy(step, s, sl);
    step[sl] = '\0';
    if (!api->summon || !ws_up()) return 1;

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return 1;
    {
        BOOL one = TRUE;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    }
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET;
    ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ad.sin_port = htons((u_short)port);
    if (bind(ls, (struct sockaddr *)&ad, sizeof(ad)) != 0 ||
        listen(ls, 8) != 0) {
        closesocket(ls);
        return 1;
    }
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(ls, &rd);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        int r = select(0, &rd, NULL, NULL, &tv);
        if (r <= 0) continue;
        SOCKET c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        DWORD tmo = 10000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof(tmo));
        char *req = NULL;
        size_t reqlen = 0;
        const char *body = "fail";
        size_t bodylen = 4;
        int code = 200;
        const char *phrase = "OK";
        char *owned = NULL;
        VxOpVal *rv = NULL;
        if (read_request(c, &req, &reqlen)) {
            VxOpVal *arg = api->make_text(req, reqlen);
            if (arg) {
                VxOpVal *args[1] = { arg };
                if (api->summon(step, args, 1, &rv) != 0) {
                    if (rv) {
                        api->release(rv);
                        rv = NULL;
                    }
                    code = 500;
                    phrase = "Step Fell";
                } else if (!rv || !api->is_true(rv)) {
                    /* dry step lands the server */
                    if (rv) api->release(rv);
                    free(req);
                    closesocket(c);
                    break;
                } else {
                    const char *bb = NULL;
                    size_t bl = 0;
                    if (api->as_text(rv, &bb, &bl)) {
                        body = bb;
                        bodylen = bl;
                    } else {
                        body = "fail";
                        bodylen = 4;
                    }
                    owned = NULL; /* borrowed from rv, freed below */
                }
            }
            free(req);
        }
        char head[256];
        int hn = snprintf(head, sizeof(head),
                          "HTTP/1.1 %d %s\r\nContent-Length: %u\r\n"
                          "Connection: close\r\n"
                          "Content-Type: text/plain; charset=utf-8\r\n\r\n",
                          code, phrase, (unsigned)bodylen);
        /* copy body: it borrows rv which we release after send */
        char *full = (char *)malloc((size_t)hn + bodylen);
        if (full) {
            memcpy(full, head, (size_t)hn);
            memcpy(full + hn, body, bodylen);
            send_all(c, full, (size_t)hn + bodylen);
            free(full);
        }
        (void)owned;
        if (rv) api->release(rv);
        closesocket(c);
    }
    closesocket(ls);
    *out = api->make_num(1);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "get", 1, f_get },
    { "serve", 2, f_serve },
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
    info.name = "VexNet";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
