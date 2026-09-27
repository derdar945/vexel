#define _CRT_SECURE_NO_WARNINGS
#include "opman.h"
#include "operator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#include <windows.h>
#ifdef _WIN32
#include <wininet.h>
#endif

/* ---------------- paths ---------------- */

int vx_store_path(char *buf, size_t cap) {
    const char *env = getenv("VEXEL_OPS");
    if (env && env[0]) {
        snprintf(buf, cap, "%s", env);
        return 1;
    }
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) return 0;
    char *slash = strrchr(exe, '\\');
    if (slash) *slash = '\0';
    else strcpy(exe, ".");
    snprintf(buf, cap, "%s\\operators", exe);
    return 1;
}

int vx_source_dir(const char *name, char *buf, size_t cap) {
    const char *cands[2];
    char a[MAX_PATH], b[MAX_PATH];
    snprintf(a, sizeof(a), "operators\\%s", name);
    snprintf(b, sizeof(b), "%s", name);
    cands[0] = a;
    cands[1] = b;
    for (int i = 0; i < 2; i++) {
        char mf[MAX_PATH];
        snprintf(mf, sizeof(mf), "%s\\operator.vxop", cands[i]);
        FILE *f = fopen(mf, "r");
        if (f) {
            fclose(f);
            snprintf(buf, cap, "%s", cands[i]);
            return 1;
        }
    }
    return 0;
}

/* ---------------- manifest ---------------- */

void vx_manifest_free(VxManifest *m) {
    if (!m) return;
    free(m->provides);
    free(m->deps);
    memset(m, 0, sizeof(*m));
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e - 1))) *--e = '\0';
    return s;
}

static int parse_provides(const char *v, VxManifest *m, VxError *err) {
    /* "window/3, show/1" */
    char *tmp = vx_strdup(v);
    if (!tmp) return 0;
    char *p = tmp;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        char *tok = p;
        while (*p && *p != ',') p++;
        if (*p) *p++ = '\0';
        char *t = trim(tok);
        if (!*t) continue;
        char *sl = strchr(t, '/');
        int arity = 0;
        if (sl) {
            *sl = '\0';
            arity = atoi(sl + 1);
        }
        t = trim(t);
        if (!*t) {
            vx_error_set(err, 0, 0, "bad provides entry");
            free(tmp);
            return 0;
        }
        VxOpProvide *np = (VxOpProvide *)realloc(
            m->provides, (size_t)(m->nprovides + 1) * sizeof(VxOpProvide));
        if (!np) { free(tmp); return 0; }
        m->provides = np;
        snprintf(m->provides[m->nprovides].name,
                 sizeof(m->provides[m->nprovides].name), "%s", t);
        m->provides[m->nprovides].arity = arity;
        m->nprovides++;
    }
    free(tmp);
    return 1;
}

static int parse_deps(const char *v, VxManifest *m, VxError *err) {
    /* "VexNet >= 1.0, VexGame" */
    (void)err;
    char *tmp = vx_strdup(v);
    if (!tmp) return 0;
    char *p = tmp;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        char *tok = p;
        while (*p && *p != ',') p++;
        if (*p) *p++ = '\0';
        char *t = trim(tok);
        if (!*t) continue;
        /* split name [op] [ver] */
        char name[64] = {0}, op[3] = {0}, ver[32] = {0};
        char *sp = strpbrk(t, " \t");
        if (!sp) {
            snprintf(name, sizeof(name), "%s", t);
        } else {
            *sp = '\0';
            snprintf(name, sizeof(name), "%s", trim(t));
            char *rest = trim(sp + 1);
            if (strncmp(rest, ">=", 2) == 0 || strncmp(rest, "<=", 2) == 0 ||
                strncmp(rest, "==", 2) == 0 || strncmp(rest, "!=", 2) == 0) {
                snprintf(op, sizeof(op), "%c%c", rest[0], rest[1]);
                snprintf(ver, sizeof(ver), "%s", trim(rest + 2));
            } else if (*rest == '>' || *rest == '<' || *rest == '=') {
                snprintf(op, sizeof(op), "%c", *rest);
                snprintf(ver, sizeof(ver), "%s", trim(rest + 1));
            } else {
                snprintf(ver, sizeof(ver), "%s", rest);
                snprintf(op, sizeof(op), "==");
            }
        }
        VxOpDep *nd = (VxOpDep *)realloc(
            m->deps, (size_t)(m->ndeps + 1) * sizeof(VxOpDep));
        if (!nd) { free(tmp); return 0; }
        m->deps = nd;
        snprintf(m->deps[m->ndeps].name, sizeof(m->deps[m->ndeps].name), "%s", name);
        snprintf(m->deps[m->ndeps].op, sizeof(m->deps[m->ndeps].op), "%s", op);
        snprintf(m->deps[m->ndeps].ver, sizeof(m->deps[m->ndeps].ver), "%s", ver);
        m->ndeps++;
    }
    free(tmp);
    return 1;
}

int vx_manifest_read(const char *dir, VxManifest *m, VxError *err) {
    memset(m, 0, sizeof(*m));
    snprintf(m->platform, sizeof(m->platform), "any");
    snprintf(m->arch, sizeof(m->arch), "any");
    snprintf(m->type, sizeof(m->type), "native");
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\operator.vxop", dir);
    FILE *f = fopen(path, "r");
    if (!f) {
        vx_error_set(err, 0, 0, "no operator.vxop in %s", dir);
        return 0;
    }
    char line[1024];
    int lno = 0, ok = 1;
    while (fgets(line, sizeof(line), f)) {
        lno++;
        char *t = trim(line);
        if (!*t || *t == '#' || *t == ';') continue;
        char *eq = strchr(t, '=');
        if (!eq) {
            vx_error_set(err, lno, 0, "bad line (want key = value)");
            ok = 0;
            break;
        }
        *eq = '\0';
        char *k = trim(t), *v = trim(eq + 1);
        /* strip quotes */
        size_t L = strlen(v);
        if (L >= 2 && ((v[0] == '"' && v[L - 1] == '"') ||
                       (v[0] == '\'' && v[L - 1] == '\''))) {
            v[L - 1] = '\0';
            v++;
        }
        if (strcmp(k, "name") == 0) snprintf(m->name, sizeof(m->name), "%s", v);
        else if (strcmp(k, "version") == 0) snprintf(m->version, sizeof(m->version), "%s", v);
        else if (strcmp(k, "description") == 0) snprintf(m->description, sizeof(m->description), "%s", v);
        else if (strcmp(k, "type") == 0) snprintf(m->type, sizeof(m->type), "%s", v);
        else if (strcmp(k, "vexel_version") == 0) snprintf(m->vexel_version, sizeof(m->vexel_version), "%s", v);
        else if (strcmp(k, "platform") == 0) snprintf(m->platform, sizeof(m->platform), "%s", v);
        else if (strcmp(k, "architecture") == 0 || strcmp(k, "arch") == 0)
            snprintf(m->arch, sizeof(m->arch), "%s", v);
        else if (strcmp(k, "entry") == 0) snprintf(m->entry, sizeof(m->entry), "%s", v);
        else if (strcmp(k, "provides") == 0) {
            if (!parse_provides(v, m, err)) { ok = 0; break; }
        } else if (strcmp(k, "dependencies") == 0) {
            if (!parse_deps(v, m, err)) { ok = 0; break; }
        }
        /* unknown keys ignored (forward compat) */
    }
    fclose(f);
    if (!ok) { vx_manifest_free(m); return 0; }
    if (!m->name[0] || !m->version[0]) {
        vx_error_set(err, 0, 0, "operator.vxop must have name + version");
        vx_manifest_free(m);
        return 0;
    }
    if (strcmp(m->type, "native") == 0 && !m->entry[0]) {
        vx_error_set(err, 0, 0, "native operator needs entry = Something.dll");
        vx_manifest_free(m);
        return 0;
    }
    return 1;
}

/* ---------------- versions ---------------- */

static long ver_part(const char **p) {
    long v = 0;
    while (**p && isdigit((unsigned char)**p)) {
        v = v * 10 + (**p - '0');
        (*p)++;
    }
    if (**p == '.') (*p)++;
    return v;
}

int vx_semver_cmp(const char *a, const char *b) {
    if (!a || !*a) return (!b || !*b) ? 0 : -1;
    if (!b || !*b) return 1;
    for (int i = 0; i < 4; i++) {
        long x = ver_part(&a), y = ver_part(&b);
        if (x < y) return -1;
        if (x > y) return 1;
        if (!*a && !*b) return 0;
    }
    return 0;
}

int vx_dep_ok(const char *have, const char *op, const char *want) {
    if (!op || !*op || !want || !*want) return 1;
    int c = vx_semver_cmp(have, want);
    if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0) return c == 0;
    if (strcmp(op, ">=") == 0) return c >= 0;
    if (strcmp(op, "<=") == 0) return c <= 0;
    if (strcmp(op, ">") == 0) return c > 0;
    if (strcmp(op, "<") == 0) return c < 0;
    if (strcmp(op, "!=") == 0) return c != 0;
    return 0;
}

static const char *arch_now(void) {
#if defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "?";
#endif
}

/* ---------------- helpers ---------------- */

static int is_dir(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int copy_file(const char *src, const char *dst, VxError *err) {
    /* same file (dev dir == store)? never copy onto itself */
    {
        char fs[MAX_PATH], fd[MAX_PATH];
        if (GetFullPathNameA(src, sizeof(fs), fs, NULL) &&
            GetFullPathNameA(dst, sizeof(fd), fd, NULL) &&
            _stricmp(fs, fd) == 0)
            return 1;
    }
    FILE *a = fopen(src, "rb");
    if (!a) {
        vx_error_set(err, 0, 0, "cannot read %s", src);
        return 0;
    }
    FILE *b = fopen(dst, "wb");
    if (!b) {
        fclose(a);
        vx_error_set(err, 0, 0, "cannot write %s", dst);
        return 0;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0) {
        if (fwrite(buf, 1, n, b) != n) {
            fclose(a);
            fclose(b);
            vx_error_set(err, 0, 0, "cannot write %s", dst);
            return 0;
        }
    }
    fclose(a);
    fclose(b);
    return 1;
}

static int rm_dir(const char *dir) {
    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                continue;
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                rm_dir(full);
                RemoveDirectoryA(full);
            } else {
                DeleteFileA(full);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryA(dir);
}

static void head(const char *fmt_title) {
    printf("\n  VEXEL\n\n");
    (void)fmt_title;
}

/* ---------------- list/info/search ---------------- */

int vx_opm_list(void) {
    char store[MAX_PATH];
    if (!vx_store_path(store, sizeof(store))) return 1;
    head(NULL);
    printf("  Installed operators:\n\n");
    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s\\*", store);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                continue;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            char dir[MAX_PATH];
            snprintf(dir, sizeof(dir), "%s\\%s", store, fd.cFileName);
            VxManifest m;
            VxError err;
            memset(&err, 0, sizeof(err));
            if (!vx_manifest_read(dir, &m, &err)) continue;
            printf("    %-14s %s  (%s)\n", m.name, m.version, m.type);
            vx_manifest_free(&m);
            n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (!n) printf("    (none — try !vex_add VexGUI)\n");
    printf("\n");
    return 0;
}

int vx_opm_info(const char *name) {
    char store[MAX_PATH];
    vx_store_path(store, sizeof(store));
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\%s", store, name);
    VxManifest m;
    VxError err;
    memset(&err, 0, sizeof(err));
    if (!vx_manifest_read(dir, &m, &err)) {
        printf("\n  VEXEL\n\n  Operator not found: %s\n\n", name);
        return 1;
    }
    printf("\n  VEXEL\n\n");
    printf("  Operator:    %s\n", m.name);
    printf("  Version:     %s\n", m.version);
    printf("  Type:        %s\n", m.type);
    printf("  Vexel:       >= %s\n",
           m.vexel_version[0] ? m.vexel_version : "0.1.0");
    printf("  Platform:    %s / %s\n", m.platform, m.arch);
    if (m.description[0]) printf("  About:       %s\n", m.description);
    if (strcmp(m.type, "native") == 0) printf("  Entry:       %s\n", m.entry);
    if (m.ndeps) {
        printf("  Depends:\n");
        for (int i = 0; i < m.ndeps; i++)
            printf("    - %s %s %s\n", m.deps[i].name, m.deps[i].op,
                   m.deps[i].ver);
    }
    if (m.nprovides) {
        printf("  Provides:\n");
        for (int i = 0; i < m.nprovides; i++)
            printf("    - %s #  (%d)\n", m.provides[i].name,
                   m.provides[i].arity);
    }
    printf("\n");
    vx_manifest_free(&m);
    return 0;
}

int vx_opm_search(const char *term) {
    printf("\n  VEXEL\n\n  Search: %s\n\n", term);
    int n = 0;
    /* installed */
    char store[MAX_PATH];
    if (vx_store_path(store, sizeof(store))) {
        char pat[MAX_PATH];
        snprintf(pat, sizeof(pat), "%s\\*", store);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                if (strcmp(fd.cFileName, ".") == 0 ||
                    strcmp(fd.cFileName, "..") == 0)
                    continue;
                char dir[MAX_PATH];
                snprintf(dir, sizeof(dir), "%s\\%s", store, fd.cFileName);
                VxManifest m;
                VxError err;
                memset(&err, 0, sizeof(err));
                if (!vx_manifest_read(dir, &m, &err)) continue;
                if (strstr(m.name, term) ||
                    (m.description[0] && strstr(m.description, term))) {
                    printf("    %-14s %s  [installed]\n", m.name, m.version);
                    n++;
                }
                vx_manifest_free(&m);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    /* sources on disk */
    {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA("operators\\*", &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                if (strcmp(fd.cFileName, ".") == 0 ||
                    strcmp(fd.cFileName, "..") == 0)
                    continue;
                char dir[MAX_PATH];
                snprintf(dir, sizeof(dir), "operators\\%s", fd.cFileName);
                VxManifest m;
                VxError err;
                memset(&err, 0, sizeof(err));
                if (!vx_manifest_read(dir, &m, &err)) continue;
                if (strstr(m.name, term) ||
                    (m.description[0] && strstr(m.description, term))) {
                    printf("    %-14s %s\n", m.name, m.version);
                    n++;
                }
                vx_manifest_free(&m);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    if (!n) printf("    (nothing found)\n");
    printf("\n");
    return 0;
}

/* ---------------- install ---------------- */

#define OPM_STACK 16

typedef struct OpCtx {
    char stack[OPM_STACK][64];
    int depth;
    char store[MAX_PATH];
} OpCtx;

static int install_one(OpCtx *ctx, const char *name, VxError *err);
static int install_dir(OpCtx *ctx, const char *name, const char *src,
                       VxError *err);

/* Fetch a http(s) URL into a file. 1 ok. Plain WinINet, no TLS games:
 * whatever the system negotiates. Non-Windows: honest refusal. */
static int http_fetch(const char *url, const char *dst, VxError *err) {
#ifdef _WIN32
    HINTERNET net = InternetOpenA("Vexel/0.1", INTERNET_OPEN_TYPE_PRECONFIG,
                                  NULL, NULL, 0);
    if (!net) {
        vx_error_set(err, 0, 0, "network down");
        return 0;
    }
    HINTERNET f = InternetOpenUrlA(net, url, NULL, 0,
                                   INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
                                   0);
    if (!f) {
        InternetCloseHandle(net);
        vx_error_set(err, 0, 0, "cannot fetch %s", url);
        return 0;
    }
    FILE *o = fopen(dst, "wb");
    if (!o) {
        InternetCloseHandle(f);
        InternetCloseHandle(net);
        vx_error_set(err, 0, 0, "cannot write %s", dst);
        return 0;
    }
    char buf[65536];
    DWORD got = 0;
    int ok = 1;
    for (;;) {
        if (!InternetReadFile(f, buf, sizeof(buf), &got)) {
            ok = 0;
            break;
        }
        if (got == 0) break;
        if (fwrite(buf, 1, got, o) != got) {
            ok = 0;
            break;
        }
    }
    fclose(o);
    InternetCloseHandle(f);
    InternetCloseHandle(net);
    if (!ok) vx_error_set(err, 0, 0, "broken download %s", url);
    return ok;
#else
    (void)dst;
    vx_error_set(err, 0, 0, "http fetch needs Windows (see PORTING.md): %s", url);
    return 0;
#endif
}

static int is_url(const char *s) {
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0;
}

/* registry roots live in <store>/registries.txt, one per line */
static void registry_path(char *buf, size_t cap, OpCtx *ctx) {
    snprintf(buf, cap, "%s\\registries.txt", ctx->store);
}

/* Try registries for a source dir of `name`. Local roots resolve to a
 * dir; http roots download manifest+entry into %TEMP%/vexdl/<Name>.
 * Returns 1 with src filled, else 0 (err set only on hard failure). */
static int registry_source(OpCtx *ctx, const char *name, char *src,
                           size_t cap, VxError *err) {
    char rp[MAX_PATH];
    registry_path(rp, sizeof(rp), ctx);
    FILE *f = fopen(rp, "r");
    if (!f) return 0;
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        char *t = trim(line);
        if (!*t || *t == '#' || *t == ';') continue;
        if (is_url(t)) {
            /* <base>/<Name>/operator.vxop + entry */
            char base[1024], mf[1100];
            snprintf(base, sizeof(base), "%s", t);
            size_t L = strlen(base);
            while (L && base[L - 1] == '/') base[--L] = '\0';
            snprintf(mf, sizeof(mf), "%s/%s/operator.vxop", base, name);
            char tmpd[MAX_PATH], tmpm[MAX_PATH];
            snprintf(tmpd, sizeof(tmpd), "%s\\vexdl\\%s", getenv("TEMP") ? getenv("TEMP") : ".", name);
            /* wipe stale temp */
            rm_dir(tmpd);
            _mkdir(getenv("TEMP") ? getenv("TEMP") : ".");
            {
                char vexdl[MAX_PATH];
                snprintf(vexdl, sizeof(vexdl), "%s\\vexdl", getenv("TEMP") ? getenv("TEMP") : ".");
                _mkdir(vexdl);
            }
            _mkdir(tmpd);
            snprintf(tmpm, sizeof(tmpm), "%s\\operator.vxop", tmpd);
            VxError derr;
            memset(&derr, 0, sizeof(derr));
            if (!http_fetch(mf, tmpm, &derr)) continue;
            VxManifest m;
            if (!vx_manifest_read(tmpd, &m, &derr)) continue;
            if (strcmp(m.name, name) != 0) {
                vx_manifest_free(&m);
                continue;
            }
            if (strcmp(m.type, "native") == 0 && m.entry[0]) {
                char eu[1100], ed[MAX_PATH];
                snprintf(eu, sizeof(eu), "%s/%s/%s", base, name, m.entry);
                snprintf(ed, sizeof(ed), "%s\\%s", tmpd, m.entry);
                if (!http_fetch(eu, ed, &derr)) {
                    vx_manifest_free(&m);
                    continue;
                }
            }
            vx_manifest_free(&m);
            snprintf(src, cap, "%s", tmpd);
            found = 1;
            break;
        } else {
            char cand[MAX_PATH], mfile[MAX_PATH];
            snprintf(cand, sizeof(cand), "%s\\%s", t, name);
            snprintf(mfile, sizeof(mfile), "%s\\operator.vxop", cand);
            FILE *probe = fopen(mfile, "r");
            if (probe) {
                fclose(probe);
                snprintf(src, cap, "%s", cand);
                found = 1;
                break;
            }
        }
    }
    fclose(f);
    if (!found && err && 0) vx_error_set(err, 0, 0, "not in registries");
    return found;
}

static int install_one(OpCtx *ctx, const char *name, VxError *err) {
    /* cycle check */
    for (int i = 0; i < ctx->depth; i++) {
        if (strcmp(ctx->stack[i], name) == 0) {
            vx_error_set(err, 0, 0, "Circular dependency: ");
            size_t L = strlen(err->msg);
            for (int j = i; j < ctx->depth; j++) {
                snprintf(err->msg + L, sizeof(err->msg) - L, "%s -> ",
                         ctx->stack[j]);
                L = strlen(err->msg);
            }
            snprintf(err->msg + L, sizeof(err->msg) - L, "%s", name);
            return 0;
        }
    }
    /* already installed with good version? check later per-dep */
    char src[MAX_PATH];
    if (!vx_source_dir(name, src, sizeof(src))) {
        if (!registry_source(ctx, name, src, sizeof(src), err)) {
            vx_error_set(err, 0, 0, "Operator not found: %s", name);
            return 0;
        }
    }
    return install_dir(ctx, name, src, err);
}

static int install_dir(OpCtx *ctx, const char *name, const char *src,
                       VxError *err) {
    VxManifest m;
    if (!vx_manifest_read(src, &m, err)) return 0;
    if (strcmp(m.name, name) != 0) {
        vx_error_set(err, 0, 0, "Invalid operator in %s: manifest says '%s'",
                     src, m.name);
        vx_manifest_free(&m);
        return 0;
    }
    /* core compat */
    if (m.vexel_version[0] &&
        vx_semver_cmp(VEXEL_VERSION, m.vexel_version) < 0) {
        vx_error_set(err, 0, 0,
                     "Operator ABI mismatch: %s wants vexel >= %s, have %s",
                     m.name, m.vexel_version, VEXEL_VERSION);
        vx_manifest_free(&m);
        return 0;
    }
    /* platform */
    if (strcmp(m.platform, "any") != 0 &&
        strcmp(m.platform, "windows") != 0) {
        vx_error_set(err, 0, 0, "Operator platform mismatch: %s needs %s",
                     m.name, m.platform);
        vx_manifest_free(&m);
        return 0;
    }
    if (strcmp(m.arch, "any") != 0 && strcmp(m.arch, arch_now()) != 0) {
        vx_error_set(err, 0, 0, "Operator arch mismatch: %s needs %s",
                     m.name, m.arch);
        vx_manifest_free(&m);
        return 0;
    }
    /* deps first */
    if (ctx->depth + 1 >= OPM_STACK) {
        vx_error_set(err, 0, 0, "dependency chain too deep");
        vx_manifest_free(&m);
        return 0;
    }
    snprintf(ctx->stack[ctx->depth], 64, "%s", name);
    ctx->depth++;
    for (int i = 0; i < m.ndeps; i++) {
        char idir[MAX_PATH];
        snprintf(idir, sizeof(idir), "%s\\%s", ctx->store, m.deps[i].name);
        VxManifest im;
        VxError derr;
        memset(&derr, 0, sizeof(derr));
        int have = vx_manifest_read(idir, &im, &derr);
        if (have && vx_dep_ok(im.version, m.deps[i].op, m.deps[i].ver)) {
            vx_manifest_free(&im);
            continue;
        }
        if (have) vx_manifest_free(&im);
        if (!install_one(ctx, m.deps[i].name, err)) {
            if (!err->has)
                vx_error_set(err, 0, 0, "Missing dependency:\n%s %s %s",
                             m.deps[i].name, m.deps[i].op, m.deps[i].ver);
            else if (strstr(err->msg, "Operator not found") ==
                     err->msg) {
                vx_error_set(err, 0, 0, "Missing dependency:\n%s %s %s",
                             m.deps[i].name, m.deps[i].op, m.deps[i].ver);
            }
            ctx->depth--;
            vx_manifest_free(&m);
            return 0;
        }
        /* re-check installed version satisfies */
        if (!vx_manifest_read(idir, &im, &derr) ||
            !vx_dep_ok(im.version, m.deps[i].op, m.deps[i].ver)) {
            if (!derr.has)
                vx_error_set(err, 0, 0, "Missing dependency:\n%s %s %s",
                             m.deps[i].name, m.deps[i].op, m.deps[i].ver);
            else {
                *err = derr;
            }
            vx_manifest_free(&im);
            ctx->depth--;
            vx_manifest_free(&m);
            return 0;
        }
        vx_manifest_free(&im);
    }
    ctx->depth--;

    /* copy into store */
    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), "%s\\%s", ctx->store, m.name);
    _mkdir(ctx->store);
    _mkdir(dst);
    /* dev dir can BE the store entry: never wipe the source itself */
    int wipe_on_fail = 1;
    {
        char fs[MAX_PATH], fd[MAX_PATH];
        if (GetFullPathNameA(src, sizeof(fs), fs, NULL) &&
            GetFullPathNameA(dst, sizeof(fd), fd, NULL) &&
            _stricmp(fs, fd) == 0)
            wipe_on_fail = 0;
    }
    printf("\n  VEXEL\n\n");
    printf("  Operator: %s\n", m.name);
    printf("  Version:  %s\n\n", m.version);
    printf("  Installing...\n\n");
    printf("  [OK] Metadata\n");
    char sMF[MAX_PATH], dMF[MAX_PATH];
    snprintf(sMF, sizeof(sMF), "%s\\operator.vxop", src);
    snprintf(dMF, sizeof(dMF), "%s\\operator.vxop", dst);
    if (!copy_file(sMF, dMF, err)) {
        if (wipe_on_fail) rm_dir(dst);
        vx_manifest_free(&m);
        return 0;
    }
    if (m.ndeps) printf("  [OK] Dependencies\n");
    else printf("  [OK] Dependencies (none)\n");
    if (strcmp(m.type, "native") == 0) {
        char sE[MAX_PATH], dE[MAX_PATH];
        snprintf(sE, sizeof(sE), "%s\\%s", src, m.entry);
        snprintf(dE, sizeof(dE), "%s\\%s", dst, m.entry);
        FILE *t = fopen(sE, "rb");
        if (!t) {
            vx_error_set(err, 0, 0,
                         "Native module not built: %s\nRun !vex_operator_build %s first",
                         sE, src);
            if (wipe_on_fail) rm_dir(dst);
            vx_manifest_free(&m);
            return 0;
        }
        fclose(t);
        if (!copy_file(sE, dE, err)) {
            if (wipe_on_fail) rm_dir(dst);
            vx_manifest_free(&m);
            return 0;
        }
        printf("  [OK] Native module\n");
    }
    printf("  [OK] Registration\n\n");
    printf("  %s installed.\n\n", m.name);
    vx_manifest_free(&m);
    return 1;
}

int vx_opm_add(const char *name) {
    OpCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (!vx_store_path(ctx.store, sizeof(ctx.store))) {
        printf("cannot locate operator store\n");
        return 1;
    }
    VxError err;
    memset(&err, 0, sizeof(err));
    if (!install_one(&ctx, name, &err)) {
        printf("\n  VEXEL\n\n  %s\n\n", err.msg);
        return 1;
    }
    return 0;
}

int vx_opm_update(const char *name) {
    /* update = reinstall from sources */
    char store[MAX_PATH];
    vx_store_path(store, sizeof(store));
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\%s", store, name);
    if (!is_dir(dir)) {
        printf("\n  VEXEL\n\n  Operator not found: %s\n\n", name);
        return 1;
    }
    return vx_opm_add(name);
}

int vx_opm_remove(const char *name) {
    char store[MAX_PATH];
    vx_store_path(store, sizeof(store));
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\%s", store, name);
    if (!is_dir(dir)) {
        printf("\n  VEXEL\n\n  Operator not found: %s\n\n", name);
        return 1;
    }
    /* refuse if others depend on it */
    {
        char pat[MAX_PATH];
        snprintf(pat, sizeof(pat), "%s\\*", store);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                if (strcmp(fd.cFileName, ".") == 0 ||
                    strcmp(fd.cFileName, "..") == 0 ||
                    strcmp(fd.cFileName, name) == 0)
                    continue;
                char od[MAX_PATH];
                snprintf(od, sizeof(od), "%s\\%s", store, fd.cFileName);
                VxManifest m;
                VxError err;
                memset(&err, 0, sizeof(err));
                if (!vx_manifest_read(od, &m, &err)) continue;
                for (int i = 0; i < m.ndeps; i++) {
                    if (strcmp(m.deps[i].name, name) == 0) {
                        printf("\n  VEXEL\n\n  Cannot remove %s: %s needs it\n\n",
                               name, m.name);
                        vx_manifest_free(&m);
                        FindClose(h);
                        return 1;
                    }
                }
                vx_manifest_free(&m);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    rm_dir(dir);
    printf("\n  VEXEL\n\n  %s removed.\n\n", name);
    return 0;
}

int vx_opm_install_path(const char *path) {
    VxManifest m;
    VxError err;
    memset(&err, 0, sizeof(err));
    if (!vx_manifest_read(path, &m, &err)) {
        printf("\n  VEXEL\n\n  Invalid operator in %s: %s\n\n", path,
               err.has ? err.msg : "bad manifest");
        return 1;
    }
    char name[64];
    snprintf(name, sizeof(name), "%s", m.name);
    vx_manifest_free(&m);
    /* install from explicit path: temporarily support by CWD trick —
       copy source lookup via current dir entries. Simplest real path:
       require the dir be reachable; reuse install_one by checking name
       resolves. If the manifest dir itself is the source, allow it. */
    char mf[MAX_PATH];
    snprintf(mf, sizeof(mf), "%s\\operator.vxop", path);
    FILE *f = fopen(mf, "r");
    if (!f) {
        printf("\n  VEXEL\n\n  Invalid operator: %s\n\n", path);
        return 1;
    }
    fclose(f);
    /* direct copy path (deps resolved by name afterwards) */
    OpCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    vx_store_path(ctx.store, sizeof(ctx.store));
    /* resolve deps by name from store/sources first */
    VxManifest sm;
    memset(&err, 0, sizeof(err));
    vx_manifest_read(path, &sm, &err);
    for (int i = 0; i < sm.ndeps; i++) {
        char idir[MAX_PATH];
        snprintf(idir, sizeof(idir), "%s\\%s", ctx.store, sm.deps[i].name);
        VxManifest im;
        VxError derr;
        memset(&derr, 0, sizeof(derr));
        int have = vx_manifest_read(idir, &im, &derr);
        if (have && vx_dep_ok(im.version, sm.deps[i].op, sm.deps[i].ver)) {
            vx_manifest_free(&im);
            continue;
        }
        if (have) vx_manifest_free(&im);
        if (!install_one(&ctx, sm.deps[i].name, &err)) {
            printf("\n  VEXEL\n\n  Missing dependency:\n  %s %s %s\n\n",
                   sm.deps[i].name, sm.deps[i].op, sm.deps[i].ver);
            vx_manifest_free(&sm);
            return 1;
        }
    }
    /* copy */
    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), "%s\\%s", ctx.store, sm.name);
    _mkdir(ctx.store);
    _mkdir(dst);
    char sMF[MAX_PATH], dMF[MAX_PATH];
    snprintf(sMF, sizeof(sMF), "%s\\operator.vxop", path);
    snprintf(dMF, sizeof(dMF), "%s\\operator.vxop", dst);
    VxError cerr;
    memset(&cerr, 0, sizeof(cerr));
    int ok = copy_file(sMF, dMF, &cerr);
    if (ok && strcmp(sm.type, "native") == 0) {
        char sE[MAX_PATH], dE[MAX_PATH];
        snprintf(sE, sizeof(sE), "%s\\%s", path, sm.entry);
        snprintf(dE, sizeof(dE), "%s\\%s", dst, sm.entry);
        ok = copy_file(sE, dE, &cerr);
    }
    if (!ok) {
        printf("\n  VEXEL\n\n  %s\n\n", cerr.msg);
        vx_manifest_free(&sm);
        return 1;
    }
    printf("\n  VEXEL\n\n  %s installed from %s.\n\n", sm.name, path);
    vx_manifest_free(&sm);
    (void)name;
    return 0;
}

int vx_opm_installed(const char *name, VxManifest *m, VxError *err) {
    char store[MAX_PATH];
    if (!vx_store_path(store, sizeof(store))) {
        vx_error_set(err, 0, 0, "cannot locate operator store");
        return 0;
    }
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\%s", store, name);
    return vx_manifest_read(dir, m, err);
}

int vx_opm_registry_add(const char *root) {
    if (!root || !*root) {
        printf("vexel: !vex_registry_add wants a dir or http base\n");
        return 1;
    }
    char store[MAX_PATH];
    if (!vx_store_path(store, sizeof(store))) {
        printf("cannot locate operator store\n");
        return 1;
    }
    _mkdir(store);
    /* local roots must exist and be dirs */
    if (!is_url(root) && !is_dir(root)) {
        printf("\n  VEXEL\n\n  Bad registry root: %s\n\n", root);
        return 1;
    }
    char rp[MAX_PATH];
    snprintf(rp, sizeof(rp), "%s\\registries.txt", store);
    /* skip doubles */
    {
        FILE *f = fopen(rp, "r");
        if (f) {
            char line[1024];
            while (fgets(line, sizeof(line), f)) {
                if (strcmp(trim(line), root) == 0) {
                    fclose(f);
                    printf("\n  VEXEL\n\n  Registry already known: %s\n\n", root);
                    return 0;
                }
            }
            fclose(f);
        }
    }
    FILE *f = fopen(rp, "a");
    if (!f) {
        printf("cannot write %s\n", rp);
        return 1;
    }
    fprintf(f, "%s\n", root);
    fclose(f);
    printf("\n  VEXEL\n\n  Registry added: %s\n\n", root);
    return 0;
}

int vx_opm_registry_list(void) {
    char store[MAX_PATH];
    if (!vx_store_path(store, sizeof(store))) return 1;
    char rp[MAX_PATH];
    snprintf(rp, sizeof(rp), "%s\\registries.txt", store);
    printf("\n  VEXEL\n\n  Registries:\n\n");
    FILE *f = fopen(rp, "r");
    int n = 0;
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            char *t = trim(line);
            if (!*t || *t == '#' || *t == ';') continue;
            printf("    %s\n", t);
            n++;
        }
        fclose(f);
    }
    if (!n) printf("    (none)\n");
    printf("\n");
    return 0;
}

int vx_opm_registry_remove(const char *root) {
    char store[MAX_PATH];
    if (!vx_store_path(store, sizeof(store))) return 1;
    char rp[MAX_PATH], tmp[MAX_PATH];
    snprintf(rp, sizeof(rp), "%s\\registries.txt", store);
    snprintf(tmp, sizeof(tmp), "%s\\registries.tmp", store);
    FILE *f = fopen(rp, "r");
    if (!f) {
        printf("\n  VEXEL\n\n  Unknown registry: %s\n\n", root);
        return 1;
    }
    FILE *o = fopen(tmp, "w");
    if (!o) {
        fclose(f);
        return 1;
    }
    int dropped = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strcmp(trim(line), root) == 0) {
            dropped = 1;
            continue;
        }
        fputs(line, o);
    }
    fclose(f);
    fclose(o);
    if (!dropped) {
        DeleteFileA(tmp);
        printf("\n  VEXEL\n\n  Unknown registry: %s\n\n", root);
        return 1;
    }
    DeleteFileA(rp);
    MoveFileA(tmp, rp);
    printf("\n  VEXEL\n\n  Registry removed: %s\n\n", root);
    return 0;
}
