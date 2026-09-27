#define _CRT_SECURE_NO_WARNINGS
#include "opman.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <windows.h>

static int valid_name(const char *n) {
    if (!n || !*n || strlen(n) > 48) return 0;
    if (!((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= 'a' && n[0] <= 'z') ||
          n[0] == '_'))
        return 0;
    for (const char *p = n; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_'))
            return 0;
    }
    return 1;
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("cannot write %s\n", path);
        return 0;
    }
    fputs(text, f);
    fclose(f);
    return 1;
}

int vx_opm_new_project(const char *name) {
    if (!valid_name(name)) {
        printf("\n  VEXEL\n\n  Bad project name: %s\n\n", name ? name : "?");
        return 1;
    }
    if (CreateDirectoryA(name, NULL) == 0 &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        printf("cannot create %s\n", name);
        return 1;
    }
    char p[MAX_PATH], buf[2048];
    snprintf(p, sizeof(p), "%s\\main.vx", name);
    snprintf(buf, sizeof(buf),
             "; %s — a Vexel bench\n\n> \"Hello from %s!\"\n\n@ x : 40 + 2\n"
             "> x\n",
             name, name);
    if (!write_text(p, buf)) return 1;
    /* vexel.json marks the project root (used by editors/tooling) */
    snprintf(p, sizeof(p), "%s\\vexel.json", name);
    snprintf(buf, sizeof(buf),
             "{\n  \"name\": \"%s\",\n  \"version\": \"0.1.0\",\n"
             "  \"entry\": \"main.vx\"\n}\n",
             name);
    if (!write_text(p, buf)) return 1;
    snprintf(p, sizeof(p), "%s\\README.md", name);
    snprintf(buf, sizeof(buf),
             "# %s\n\nA Vexel project.\n\nRun: `vexel.exe !vex_run main.vx`\n",
             name);
    if (!write_text(p, buf)) return 1;
    printf("\n  VEXEL\n\n  Project %s ready.\n  Bench: %s\\main.vx\n\n", name,
           name);
    return 0;
}

int vx_opm_new_operator(const char *name) {
    if (!valid_name(name)) {
        printf("\n  VEXEL\n\n  Bad operator name: %s\n\n", name ? name : "?");
        return 1;
    }
    char dir[MAX_PATH], sub[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s", name);
    CreateDirectoryA(dir, NULL);
    snprintf(sub, sizeof(sub), "%s\\src", dir);
    CreateDirectoryA(sub, NULL);
    snprintf(sub, sizeof(sub), "%s\\examples", dir);
    CreateDirectoryA(sub, NULL);

    char p[MAX_PATH], buf[8192];
    /* manifest */
    snprintf(p, sizeof(p), "%s\\operator.vxop", dir);
    snprintf(buf, sizeof(buf),
             "# %s — Vexel operator\n"
             "name = %s\n"
             "version = 0.1.0\n"
             "description = My Vexel operator\n"
             "type = native\n"
             "vexel_version = 0.1.0\n"
             "platform = windows\n"
             "architecture = any\n"
             "entry = %s.dll\n"
             "provides = ping/1\n",
             name, name, name);
    if (!write_text(p, buf)) return 1;
    /* C template */
    snprintf(p, sizeof(p), "%s\\src\\%s.c", dir, name);
    snprintf(buf, sizeof(buf),
             "/* %s — native Vexel operator (C17). */\n"
             "#include \"operator.h\"\n"
             "#include <string.h>\n"
             "\n"
             "/* ping # x — gives x back. */\n"
             "static int op_ping(const VxOpApi *api, VxOpVal **args, int argc,\n"
             "                 VxOpVal **out) {\n"
             "    (void)argc;\n"
             "    /* NOTE: *out must be a fresh value. Never hand back\n"
             "       the args pointer itself (it borrows VM memory). */\n"
             "    *out = api->clone(args[0]);\n"
             "    return *out ? 0 : 1;\n"
             "}\n"
             "\n"
             "static const VxOpFuncInfo funcs[] = {\n"
             "    { \"ping\", 1, op_ping },\n"
             "};\n"
             "\n"
             "#ifdef _WIN32\n"
             "#define VXOP_EXPORT __declspec(dllexport)\n"
             "#else\n"
             "#define VXOP_EXPORT\n"
             "#endif\n"
             "\n"
             "VXOP_EXPORT const VxOpInfo *vxop_open(const VxOpApi *api) {\n"
             "    static VxOpInfo info;\n"
             "    (void)api;\n"
             "    info.abi_version = VXOP_ABI_VERSION;\n"
             "    info.name = \"%s\";\n"
             "    info.version = \"0.1.0\";\n"
             "    info.nfuncs = 1;\n"
             "    info.funcs = funcs;\n"
             "    return &info;\n"
             "}\n",
             name, name);
    if (!write_text(p, buf)) return 1;
    /* example */
    snprintf(p, sizeof(p), "%s\\examples\\hello.vx", dir);
    snprintf(buf, sizeof(buf),
             "; use %s\n\n@ %s\n> ping # 41 + 1\n",
             name, name);
    if (!write_text(p, buf)) return 1;
    /* readme */
    snprintf(p, sizeof(p), "%s\\README.md", dir);
    snprintf(buf, sizeof(buf),
             "# %s\n\nCustom Vexel operator.\n\n"
             "Path: creation -> API -> code -> build -> install -> use.\n\n"
             "```\n!vex_operator_build %s\n!vex_add %s\n"
             "> ping # 41   (in your .vx, after `@ %s`)\n```\n",
             name, name, name, name);
    if (!write_text(p, buf)) return 1;
    printf("\n  VEXEL\n\n  Operator %s ready.\n", name);
    printf("  Code:  %s\\src\\%s.c\n", dir, name);
    printf("  Next:  !vex_operator_build %s\n         !vex_add %s\n\n", name,
           name);
    return 0;
}

/* find Core include dir (has operator.h) walking up from dir */
static int find_include(const char *dir, char *buf, size_t cap) {
    char cur[MAX_PATH];
    snprintf(cur, sizeof(cur), "%s", dir);
    for (int i = 0; i < 6; i++) {
        char cand[MAX_PATH];
        snprintf(cand, sizeof(cand), "%s\\include\\operator.h", cur);
        FILE *f = fopen(cand, "r");
        if (f) {
            fclose(f);
            snprintf(buf, cap, "%s\\include", cur);
            return 1;
        }
        snprintf(cand, sizeof(cand), "%s\\operator.h", cur);
        f = fopen(cand, "r");
        if (f) {
            fclose(f);
            snprintf(buf, cap, "%s", cur);
            return 1;
        }
        /* up */
        char *s = strrchr(cur, '\\');
        char *s2 = strrchr(cur, '/');
        char *cut = s > s2 ? s : s2;
        if (!cut) break;
        *cut = '\0';
        if (!*cur) break;
    }
    /* exe dir fallback */
    {
        char exe[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
        if (n && n < sizeof(exe)) {
            char *s = strrchr(exe, '\\');
            if (s) *s = '\0';
            char cand[MAX_PATH];
            snprintf(cand, sizeof(cand), "%s\\include\\operator.h", exe);
            FILE *f = fopen(cand, "r");
            if (f) {
                fclose(f);
                snprintf(buf, cap, "%s\\include", exe);
                return 1;
            }
        }
    }
    return 0;
}

static int find_cc(char *buf, size_t cap) {
    /* PATH first */
    DWORD r = SearchPathA(NULL, "gcc.exe", NULL, (DWORD)cap, buf, NULL);
    if (r > 0 && r < cap) return 1;
    r = SearchPathA(NULL, "cc.exe", NULL, (DWORD)cap, buf, NULL);
    if (r > 0 && r < cap) return 1;
    const char *known[] = {
        "C:\\mingw64\\mingw64\\bin\\gcc.exe",
        "C:\\mingw64-posix\\bin\\gcc.exe",
        NULL,
    };
    for (int i = 0; known[i]; i++) {
        FILE *f = fopen(known[i], "r");
        if (f) {
            fclose(f);
            snprintf(buf, cap, "%s", known[i]);
            return 1;
        }
    }
    return 0;
}

int vx_opm_build_operator(const char *dir) {
    if (!dir || !*dir) dir = ".";
    VxManifest m;
    VxError err;
    memset(&err, 0, sizeof(err));
    if (!vx_manifest_read(dir, &m, &err)) {
        printf("\n  VEXEL\n\n  Invalid operator in %s: %s\n\n", dir,
               err.has ? err.msg : "bad manifest");
        return 1;
    }
    if (strcmp(m.type, "native") != 0) {
        printf("\n  VEXEL\n\n  Nothing to build (%s is '%s').\n\n", m.name,
               m.type);
        vx_manifest_free(&m);
        return 0;
    }
    char cc[MAX_PATH], inc[MAX_PATH];
    if (!find_cc(cc, sizeof(cc))) {
        printf("\n  VEXEL\n\n  No C compiler found (need gcc for %s).\n\n",
               m.name);
        vx_manifest_free(&m);
        return 1;
    }
    if (!find_include(dir, inc, sizeof(inc))) {
        printf("\n  VEXEL\n\n  Cannot find Core include/operator.h.\n\n");
        vx_manifest_free(&m);
        return 1;
    }
    /* gather src/*.c */
    char pat[MAX_PATH], sources[4096] = {0};
    snprintf(pat, sizeof(pat), "%s\\src\\*.c", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("\n  VEXEL\n\n  No sources in %s\\src.\n\n", dir);
        vx_manifest_free(&m);
        return 1;
    }
    do {
        char one[MAX_PATH];
        snprintf(one, sizeof(one), "%s\\src\\%s", dir, fd.cFileName);
        strncat(sources, " \"", sizeof(sources) - strlen(sources) - 1);
        strncat(sources, one, sizeof(sources) - strlen(sources) - 1);
        strncat(sources, "\"", sizeof(sources) - strlen(sources) - 1);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    char out[MAX_PATH];
    snprintf(out, sizeof(out), "%s\\%s", dir, m.entry);
    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -shared -O2 -std=c17 -I\"%s\" -o \"%s\"%s"
             " -luser32 -lgdi32",
             cc, inc, out, sources);
    printf("\n  VEXEL\n\n  Building %s...\n", m.name);
    /* CreateProcess directly: cmd /c would eat our quotes. */
    int rc = 1;
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));
        char line[8192];
        snprintf(line, sizeof(line), "%s", cmd);
        if (CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL,
                           &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            rc = (int)code;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            rc = 1;
        }
    }
    if (rc != 0) {
        printf("  Build failed.\n\n");
        vx_manifest_free(&m);
        return 1;
    }
    printf("  [OK] %s\n\n", out);
    vx_manifest_free(&m);
    return 0;
}
