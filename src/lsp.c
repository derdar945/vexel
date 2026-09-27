#include "lsp.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "opman.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* ============ tiny JSON output ============ */

typedef struct Buf {
    char *s;
    size_t len, cap;
} Buf;

static void bput(Buf *b, const char *t) {
    size_t L = strlen(t);
    if (b->len + L + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 1024;
        while (nc < b->len + L + 1) nc *= 2;
        char *nd = (char *)realloc(b->s, nc);
        if (!nd) return;
        b->s = nd;
        b->cap = nc;
    }
    memcpy(b->s + b->len, t, L);
    b->len += L;
    b->s[b->len] = '\0';
}

static void bputn(Buf *b, const char *t, size_t L) {
    if (b->len + L + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 1024;
        while (nc < b->len + L + 1) nc *= 2;
        char *nd = (char *)realloc(b->s, nc);
        if (!nd) return;
        b->s = nd;
        b->cap = nc;
    }
    memcpy(b->s + b->len, t, L);
    b->len += L;
    b->s[b->len] = '\0';
}

/* JSON string with UTF-8 passthrough + escapes */
static void bstr(Buf *b, const char *s) {
    bput(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"': bput(b, "\\\""); break;
            case '\\': bput(b, "\\\\"); break;
            case '\n': bput(b, "\\n"); break;
            case '\r': bput(b, "\\r"); break;
            case '\t': bput(b, "\\t"); break;
            default:
                if (*p < 0x20) {
                    char e[8];
                    snprintf(e, sizeof(e), "\\u%04x", *p);
                    bput(b, e);
                } else {
                    char c[2] = { (char)*p, 0 };
                    bput(b, c);
                }
        }
    }
    bput(b, "\"");
}

static void send_msg(const char *body, size_t len) {
    printf("Content-Length: %u\r\n\r\n", (unsigned)len);
    fwrite(body, 1, len, stdout);
    fflush(stdout);
}

/* ============ tiny JSON input ============ */

/* find "key" : valueStart (value = string|number|{|true...) */
static const char *jkey(const char *json, const char *key) {
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = json;
    for (;;) {
        p = strstr(p, pat);
        if (!p) return NULL;
        const char *q = p + strlen(pat);
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t') q++;
            return q;
        }
        p = q;
    }
}

static int jnum(const char *json, const char *key, long *out) {
    const char *v = jkey(json, key);
    if (!v) return 0;
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v) return 0;
    *out = n;
    return 1;
}

/* decode JSON string at v (must start with ") into malloc'd UTF-8 */
static char *jstr(const char *v) {
    if (!v || *v != '"') return NULL;
    v++;
    size_t cap = 256, len = 0;
    char *s = (char *)malloc(cap);
    if (!s) return NULL;
    while (*v && *v != '"') {
        unsigned c;
        if (*v == '\\') {
            v++;
            switch (*v) {
                case '"': c = '"'; v++; break;
                case '\\': c = '\\'; v++; break;
                case '/': c = '/'; v++; break;
                case 'n': c = '\n'; v++; break;
                case 'r': c = '\r'; v++; break;
                case 't': c = '\t'; v++; break;
                case 'u': {
                    unsigned u = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *++v;
                        u <<= 4;
                        if (h >= '0' && h <= '9') u += (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') u += (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') u += (unsigned)(h - 'A' + 10);
                        else break;
                    }
                    v++;
                    if (u >= 0xD800 && u <= 0xDBFF && v[0] == '\\' && v[1] == 'u') {
                        /* surrogate pair */
                        unsigned lo = 0;
                        v += 2;
                        for (int i = 0; i < 4; i++) {
                            char h = *v++;
                            lo <<= 4;
                            if (h >= '0' && h <= '9') lo += (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') lo += (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo += (unsigned)(h - 'A' + 10);
                        }
                        c = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    } else {
                        c = u;
                    }
                    /* encode UTF-8 below */
                    if (c < 0x80) {
                        if (len + 1 >= cap) goto grow;
                        s[len++] = (char)c;
                    } else if (c < 0x800) {
                        if (len + 2 >= cap) goto grow;
                        s[len++] = (char)(0xC0 | (c >> 6));
                        s[len++] = (char)(0x80 | (c & 0x3F));
                    } else if (c < 0x10000) {
                        if (len + 3 >= cap) goto grow;
                        s[len++] = (char)(0xE0 | (c >> 12));
                        s[len++] = (char)(0x80 | ((c >> 6) & 0x3F));
                        s[len++] = (char)(0x80 | (c & 0x3F));
                    } else {
                        if (len + 4 >= cap) goto grow;
                        s[len++] = (char)(0xF0 | (c >> 18));
                        s[len++] = (char)(0x80 | ((c >> 12) & 0x3F));
                        s[len++] = (char)(0x80 | ((c >> 6) & 0x3F));
                        s[len++] = (char)(0x80 | (c & 0x3F));
                    }
                    continue;
                }
                default: c = (unsigned char)*v++; break;
            }
            if (len + 1 >= cap) {
            grow:;
                cap *= 2;
                char *nd = (char *)realloc(s, cap);
                if (!nd) {
                    free(s);
                    return NULL;
                }
                s = nd;
            }
            s[len++] = (char)c;
            continue;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *nd = (char *)realloc(s, cap);
            if (!nd) {
                free(s);
                return NULL;
            }
            s = nd;
        }
        s[len++] = *v++;
    }
    s[len] = '\0';
    return s;
}

/* read one framed message; NULL on EOF */
static char *read_msg(void) {
    char line[256];
    long len = -1;
    for (;;) {
        if (!fgets(line, sizeof(line), stdin)) return NULL;
        if (line[0] == '\r' || line[0] == '\n') {
            if (len >= 0) break;
            continue;
        }
        if (strncmp(line, "Content-Length:", 15) == 0) len = atol(line + 15);
    }
    if (len < 0 || len > 64 * 1024 * 1024) return NULL;
    char *b = (char *)malloc((size_t)len + 1);
    if (!b) return NULL;
    size_t got = 0;
    while (got < (size_t)len) {
        size_t n = fread(b + got, 1, (size_t)len - got, stdin);
        if (n == 0) break;
        got += n;
    }
    if (got != (size_t)len) {
        free(b);
        return NULL;
    }
    b[len] = '\0';
    return b;
}

/* ============ documents ============ */

typedef struct Doc {
    char *uri;
    char *text;
    struct Doc *next;
} Doc;

static Doc *g_docs = NULL;

static Doc *doc_get(const char *uri, int create) {
    for (Doc *d = g_docs; d; d = d->next) {
        if (strcmp(d->uri, uri) == 0) return d;
    }
    if (!create) return NULL;
    Doc *d = (Doc *)calloc(1, sizeof(Doc));
    if (!d) return NULL;
    d->uri = vx_strdup(uri);
    d->text = vx_strdup("");
    d->next = g_docs;
    g_docs = d;
    return d;
}

/* ============ analysis (lex/parse/compile) ============ */

typedef struct Diag {
    int line, col; /* 0-based */
    char msg[512];
} Diag;

typedef struct Analysis {
    Diag *diags;
    int ndiags;
    VxAst ast;
    int ast_ok;
    VxProgram prog;
    int prog_ok;
} Analysis;

static void add_diag(Analysis *a, int line, int col, const char *msg) {
    Diag *nd = (Diag *)realloc(a->diags, (size_t)(a->ndiags + 1) * sizeof(Diag));
    if (!nd) return;
    a->diags = nd;
    a->diags[a->ndiags].line = line < 0 ? 0 : line;
    a->diags[a->ndiags].col = col < 0 ? 0 : col;
    snprintf(a->diags[a->ndiags].msg, sizeof(a->diags[a->ndiags].msg), "%s", msg);
    a->ndiags++;
}

static void analyze(const char *src, Analysis *a) {
    memset(a, 0, sizeof(*a));
    VxTokVec toks;
    VxError err;
    memset(&err, 0, sizeof(err));
    if (!vx_lex(src, &toks, &err)) {
        add_diag(a, err.line - 1, err.col - 1, err.msg);
        return;
    }
    memset(&a->ast, 0, sizeof(a->ast));
    if (!vx_parse(&toks, &a->ast, &err)) {
        add_diag(a, err.line - 1, err.col - 1, err.msg);
        vx_tokvec_free(&toks);
        return;
    }
    vx_tokvec_free(&toks);
    a->ast_ok = 1;
    if (!vx_compile(&a->ast, &a->prog, &err)) {
        add_diag(a, err.line - 1, err.col - 1, err.msg);
        return;
    }
    a->prog_ok = 1;
}

static void analysis_free(Analysis *a) {
    free(a->diags);
    if (a->ast_ok) vx_ast_free(&a->ast);
    if (a->prog_ok) vx_program_free(&a->prog);
    memset(a, 0, sizeof(*a));
}

/* ============ word at position ============ */

static int is_wordc(char c) {
    return (c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9'));
}

/* line (0-based) start offset + word under char (0-based UTF-16-ish byte idx) */
static char *word_at(const char *text, long line, long ch, long *wline, long *wcol) {
    const char *p = text;
    long ln = 0;
    while (ln < line && *p) {
        if (*p == '\n') ln++;
        p++;
    }
    if (ln != line) return NULL;
    const char *e = strchr(p, '\n');
    size_t L = e ? (size_t)(e - p) : strlen(p);
    if ((size_t)ch > L) ch = (long)L;
    long a = ch, b = ch;
    while (a > 0 && is_wordc(p[a - 1])) a--;
    while ((size_t)b < L && is_wordc(p[b])) b++;
    if (a == b) return NULL;
    if (wline) *wline = line;
    if (wcol) *wcol = a;
    return vx_strndup(p + a, (size_t)(b - a));
}

/* ============ AST helpers ============ */

typedef struct Sym {
    char name[64];
    int line;
    int kind; /* 0 tool, 1 cell, 2 op */
} Sym;

typedef struct SymVec {
    Sym *v;
    int n, cap;
} SymVec;

static void sym_push(SymVec *s, const char *name, int line, int kind) {
    if (s->n + 1 > s->cap) {
        int nc = s->cap ? s->cap * 2 : 16;
        Sym *nd = (Sym *)realloc(s->v, (size_t)nc * sizeof(Sym));
        if (!nd) return;
        s->v = nd;
        s->cap = nc;
    }
    snprintf(s->v[s->n].name, sizeof(s->v[s->n].name), "%s", name);
    s->v[s->n].line = line;
    s->v[s->n].kind = kind;
    s->n++;
}

static void symbols_of(VxBeat *b, SymVec *s) {
    for (; b; b = b->next) {
        if (b->kind == B_TOOL) sym_push(s, b->u.tool.name, b->line - 1, 0);
        else if (b->kind == B_CELL) sym_push(s, b->u.cell.name, b->line - 1, 1);
        else if (b->kind == B_ATTACH) sym_push(s, b->u.attach.name, b->line - 1, 2);
    }
}

static const char *builtin_doc(const char *w) {
    if (strcmp(w, "len") == 0) return "len # x — length of vec/text, else fail";
    if (strcmp(w, "at") == 0) return "at # v, i — element / 1-char text, else fail";
    if (strcmp(w, "type") == 0) return "type # x — \"number\" / \"text\" / \"vec\" / \"fail\"";
    if (strcmp(w, "str") == 0) return "str # x — text rendering";
    if (strcmp(w, "fail") == 0) return "fail — a fallen value. Silent, rescued with `??`";
    if (strcmp(w, "it") == 0) return "it — the circle number (0-based), fail outside `*`";
    if (strcmp(w, "and") == 0) return "and — wet test of both sides (eager)";
    if (strcmp(w, "or") == 0) return "or — wet test of either side (eager)";
    if (strcmp(w, "not") == 0) return "not — flips wet/dry";
    return NULL;
}

/* ============ responses ============ */

static void respond(long id, const char *result_json_or_null) {
    Buf b;
    memset(&b, 0, sizeof(b));
    char head[128];
    snprintf(head, sizeof(head), "{\"jsonrpc\":\"2.0\",\"id\":%ld,\"result\":", id);
    bput(&b, head);
    bput(&b, result_json_or_null ? result_json_or_null : "null");
    bput(&b, "}");
    send_msg(b.s ? b.s : "{}", b.len);
    free(b.s);
}

static void notify(const char *method, const char *params_json) {
    Buf b;
    memset(&b, 0, sizeof(b));
    bput(&b, "{\"jsonrpc\":\"2.0\",\"method\":");
    bstr(&b, method);
    bput(&b, ",\"params\":");
    bput(&b, params_json);
    bput(&b, "}");
    send_msg(b.s ? b.s : "{}", b.len);
    free(b.s);
}

static void publish_diags(Doc *d, Analysis *a) {
    Buf b, arr;
    memset(&b, 0, sizeof(b));
    memset(&arr, 0, sizeof(arr));
    bput(&arr, "[");
    for (int i = 0; i < a->ndiags; i++) {
        if (i) bput(&arr, ",");
        char tmp[128];
        snprintf(tmp, sizeof(tmp),
                 "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                 "\"end\":{\"line\":%d,\"character\":%d}},"
                 "\"severity\":1,\"source\":\"vexel\",\"message\":",
                 a->diags[i].line, a->diags[i].col, a->diags[i].line,
                 a->diags[i].col + 1);
        bput(&arr, tmp);
        bstr(&arr, a->diags[i].msg);
        bput(&arr, "}");
    }
    bput(&arr, "]");
    bput(&b, "{\"uri\":");
    bstr(&b, d->uri);
    bput(&b, ",\"diagnostics\":");
    bput(&b, arr.s ? arr.s : "[]");
    bput(&b, "}");
    notify("textDocument/publishDiagnostics", b.s ? b.s : "{}");
    free(b.s);
    free(arr.s);
}

/* ============ main loop ============ */

int vx_lsp_run(void) {
    /* exact framing bytes: no CRLF translation on stdio */
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    int shutdown = 0;
    for (;;) {
        char *msg = read_msg();
        if (!msg) break; /* EOF */
        const char *m = jkey(msg, "method");
        char method[64] = { 0 };
        if (m && *m == '"') {
            size_t i = 0;
            m++;
            while (*m && *m != '"' && i + 1 < sizeof(method)) method[i++] = *m++;
        }
        long id = -1;
        jnum(msg, "id", &id);
        int has_id = jkey(msg, "id") != NULL;

        if (strcmp(method, "initialize") == 0) {
            respond(id,
                    "{\"capabilities\":{\"textDocumentSync\":1,"
                    "\"hoverProvider\":true,\"completionProvider\":{},"
                    "\"definitionProvider\":true,\"documentSymbolProvider\":true},"
                    "\"serverInfo\":{\"name\":\"vexel-lsp\",\"version\":\"0.1.0\"}}");
        } else if (strcmp(method, "initialized") == 0) {
            /* no-op */
        } else if (strcmp(method, "shutdown") == 0) {
            shutdown = 1;
            if (has_id) respond(id, "null");
        } else if (strcmp(method, "exit") == 0) {
            free(msg);
            break;
        } else if (strcmp(method, "textDocument/didOpen") == 0 ||
                   strcmp(method, "textDocument/didChange") == 0) {
            const char *td = jkey(msg, "textDocument");
            char *uri = NULL, *text = NULL;
            if (td) {
                const char *u = jkey(td, "uri");
                if (u) uri = jstr(u);
            }
            if (strcmp(method, "textDocument/didOpen") == 0) {
                const char *t = td ? jkey(td, "text") : NULL;
                if (t) text = jstr(t);
            } else {
                /* full sync: first contentChanges[] entry */
                const char *cc = jkey(msg, "contentChanges");
                if (cc && *cc == '[') {
                    const char *t = jkey(cc, "text");
                    if (t) text = jstr(t);
                }
            }
            if (uri && text) {
                Doc *d = doc_get(uri, 1);
                if (d) {
                    free(d->text);
                    d->text = text;
                    text = NULL;
                    Analysis a;
                    analyze(d->text, &a);
                    publish_diags(d, &a);
                    analysis_free(&a);
                }
            }
            free(uri);
            free(text);
        } else if (strcmp(method, "textDocument/hover") == 0) {
            Buf r;
            memset(&r, 0, sizeof(r));
            bput(&r, "null");
            const char *td = jkey(msg, "textDocument");
            const char *pp = jkey(msg, "position");
            if (td && pp) {
                char *uri = NULL;
                const char *u = jkey(td, "uri");
                if (u) uri = jstr(u);
                long ln = 0, ch = 0;
                jnum(pp, "line", &ln);
                jnum(pp, "character", &ch);
                Doc *d = uri ? doc_get(uri, 0) : NULL;
                if (d) {
                    char *w = word_at(d->text, ln, ch, NULL, NULL);
                    if (w) {
                        const char *doc = builtin_doc(w);
                        Buf md;
                        memset(&md, 0, sizeof(md));
                        if (doc) {
                            bput(&md, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
                            bstr(&md, doc);
                            bput(&md, "}}");
                        } else {
                            /* tool in this bench? */
                            Analysis a;
                            analyze(d->text, &a);
                            if (a.ast_ok) {
                                SymVec sv;
                                memset(&sv, 0, sizeof(sv));
                                symbols_of(a.ast.head, &sv);
                                for (int i = 0; i < sv.n; i++) {
                                    if (strcmp(sv.v[i].name, w) == 0) {
                                        char tmp[256];
                                        const char *kind = sv.v[i].kind == 0 ? "tool" : sv.v[i].kind == 1 ? "cell" : "operator";
                                        snprintf(tmp, sizeof(tmp),
                                                 "{\"contents\":{\"kind\":\"markdown\",\"value\":");
                                        bput(&md, tmp);
                                        char txt[160];
                                        snprintf(txt, sizeof(txt), "`%s` — %s in this bench", w, kind);
                                        bstr(&md, txt);
                                        bput(&md, "}}");
                                        break;
                                    }
                                }
                                /* attached operator verbs? */
                                if (!md.s) {
                                    for (int i = 0; i < sv.n; i++) {
                                        if (sv.v[i].kind != 2) continue;
                                        VxManifest mf;
                                        VxError err;
                                        memset(&err, 0, sizeof(err));
                                        if (!vx_opm_installed(sv.v[i].name, &mf, &err)) continue;
                                        for (int j = 0; j < mf.nprovides; j++) {
                                            if (strcmp(mf.provides[j].name, w) == 0) {
                                                char txt[256];
                                                snprintf(txt, sizeof(txt), "`%s # (%d)` — %s %s",
                                                         w, mf.provides[j].arity, mf.name, mf.version);
                                                bput(&md, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
                                                bstr(&md, txt);
                                                bput(&md, "}}");
                                                break;
                                            }
                                        }
                                        vx_manifest_free(&mf);
                                        if (md.s) break;
                                    }
                                }
                                free(sv.v);
                            }
                            analysis_free(&a);
                        }
                        if (md.s) {
                            free(r.s);
                            r = md;
                        } else {
                            free(md.s);
                        }
                        free(w);
                    }
                }
                free(uri);
            }
            if (has_id) respond(id, r.s ? r.s : "null");
            free(r.s);
        } else if (strcmp(method, "textDocument/completion") == 0) {
            Buf r;
            memset(&r, 0, sizeof(r));
            bput(&r, "{\"isIncomplete\":false,\"items\":[");
            const char *td = jkey(msg, "textDocument");
            int first = 1;
            if (td) {
                char *uri = NULL;
                const char *u = jkey(td, "uri");
                if (u) uri = jstr(u);
                Doc *d = uri ? doc_get(uri, 0) : NULL;
                if (d) {
                    Analysis a;
                    analyze(d->text, &a);
                    if (a.ast_ok) {
                        SymVec sv;
                        memset(&sv, 0, sizeof(sv));
                        symbols_of(a.ast.head, &sv);
                        for (int i = 0; i < sv.n; i++) {
                            if (!first) bput(&r, ",");
                            first = 0;
                            bput(&r, "{\"label\":");
                            bstr(&r, sv.v[i].name);
                            char tmp[128];
                            snprintf(tmp, sizeof(tmp), ",\"kind\":%d,\"detail\":",
                                     sv.v[i].kind == 0 ? 3 : sv.v[i].kind == 1 ? 6 : 9);
                            bput(&r, tmp);
                            bstr(&r, sv.v[i].kind == 0 ? "& tool (this bench)" : sv.v[i].kind == 1 ? "@ cell" : "@ operator");
                            bput(&r, "}");
                        }
                        /* verbs of attached operators */
                        for (int i = 0; i < sv.n; i++) {
                            if (sv.v[i].kind != 2) continue;
                            VxManifest mf;
                            VxError err;
                            memset(&err, 0, sizeof(err));
                            if (!vx_opm_installed(sv.v[i].name, &mf, &err)) continue;
                            for (int j = 0; j < mf.nprovides; j++) {
                                if (!first) bput(&r, ",");
                                first = 0;
                                bput(&r, "{\"label\":");
                                bstr(&r, mf.provides[j].name);
                                char tmp[160];
                                snprintf(tmp, sizeof(tmp), ",\"kind\":2,\"detail\":");
                                bput(&r, tmp);
                                char det[160];
                                snprintf(det, sizeof(det), "%s # (%d) — %s",
                                         mf.provides[j].name, mf.provides[j].arity, mf.name);
                                bstr(&r, det);
                                bput(&r, "}");
                            }
                            vx_manifest_free(&mf);
                        }
                        free(sv.v);
                    }
                    analysis_free(&a);
                    /* builtins always */
                    const char *bi[] = { "len", "at", "type", "str", NULL };
                    for (int i = 0; bi[i]; i++) {
                        if (!first) bput(&r, ",");
                        first = 0;
                        bput(&r, "{\"label\":");
                        bstr(&r, bi[i]);
                        bput(&r, ",\"kind\":3,\"detail\":\"built-in\"}");
                    }
                }
                free(uri);
            }
            bput(&r, "]}");
            if (has_id) respond(id, r.s);
            free(r.s);
        } else if (strcmp(method, "textDocument/definition") == 0) {
            Buf r;
            memset(&r, 0, sizeof(r));
            bput(&r, "null");
            const char *td = jkey(msg, "textDocument");
            const char *pp = jkey(msg, "position");
            if (td && pp) {
                char *uri = NULL;
                const char *u = jkey(td, "uri");
                if (u) uri = jstr(u);
                long ln = 0, ch = 0;
                jnum(pp, "line", &ln);
                jnum(pp, "character", &ch);
                Doc *d = uri ? doc_get(uri, 0) : NULL;
                if (d) {
                    char *w = word_at(d->text, ln, ch, NULL, NULL);
                    if (w) {
                        Analysis a;
                        analyze(d->text, &a);
                        if (a.ast_ok) {
                            SymVec sv;
                            memset(&sv, 0, sizeof(sv));
                            symbols_of(a.ast.head, &sv);
                            for (int i = 0; i < sv.n; i++) {
                                if (strcmp(sv.v[i].name, w) == 0 && sv.v[i].kind == 0) {
                                    char tmp[512];
                                    snprintf(tmp, sizeof(tmp),
                                             "{\"uri\":");
                                    free(r.s);
                                    memset(&r, 0, sizeof(r));
                                    bput(&r, tmp);
                                    bstr(&r, d->uri);
                                    snprintf(tmp, sizeof(tmp),
                                             ",\"range\":{\"start\":{\"line\":%d,\"character\":0},"
                                             "\"end\":{\"line\":%d,\"character\":0}}}",
                                             sv.v[i].line, sv.v[i].line);
                                    bput(&r, tmp);
                                    break;
                                }
                            }
                            free(sv.v);
                        }
                        analysis_free(&a);
                        free(w);
                    }
                }
                free(uri);
            }
            if (has_id) respond(id, r.s ? r.s : "null");
            free(r.s);
        } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
            Buf r;
            memset(&r, 0, sizeof(r));
            bput(&r, "[");
            const char *td = jkey(msg, "textDocument");
            if (td) {
                char *uri = NULL;
                const char *u = jkey(td, "uri");
                if (u) uri = jstr(u);
                Doc *d = uri ? doc_get(uri, 0) : NULL;
                if (d) {
                    Analysis a;
                    analyze(d->text, &a);
                    if (a.ast_ok) {
                        SymVec sv;
                        memset(&sv, 0, sizeof(sv));
                        symbols_of(a.ast.head, &sv);
                        for (int i = 0; i < sv.n; i++) {
                            if (i) bput(&r, ",");
                            char tmp[256];
                            snprintf(tmp, sizeof(tmp),
                                     "{\"name\":");
                            bput(&r, tmp);
                            bstr(&r, sv.v[i].name);
                            snprintf(tmp, sizeof(tmp),
                                     ",\"kind\":%d,"
                                     "\"range\":{\"start\":{\"line\":%d,\"character\":0},"
                                     "\"end\":{\"line\":%d,\"character\":0}},"
                                     "\"selectionRange\":{\"start\":{\"line\":%d,\"character\":0},"
                                     "\"end\":{\"line\":%d,\"character\":0}}}",
                                     sv.v[i].kind == 0 ? 12 : sv.v[i].kind == 1 ? 13 : 2,
                                     sv.v[i].line, sv.v[i].line, sv.v[i].line, sv.v[i].line);
                            bput(&r, tmp);
                        }
                        free(sv.v);
                    }
                    analysis_free(&a);
                }
                free(uri);
            }
            bput(&r, "]");
            if (has_id) respond(id, r.s);
            free(r.s);
        } else {
            /* unknown: answer null to requests, silence to notifications */
            if (has_id) respond(id, "null");
        }
        free(msg);
        (void)shutdown;
    }
    return 0;
}
