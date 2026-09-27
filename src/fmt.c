#include "fmt.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Scan one line: first significant rune, code end (before comment),
 * bracket delta. Strings and ;comments respected. */
typedef struct LineInfo {
    char rune;      /* first non-space char outside strings, 0 if none */
    int opens;      /* block opener: rune in ?&* and code ends with ':' */
    int closes;     /* line is exactly . or ! (+comment) */
    int bdelta;     /* bracket depth change from []() pairs */
    size_t code_end; /* index where code stops (comment/line end) */
} LineInfo;

static LineInfo scan_line(const char *s, size_t len) {
    LineInfo li;
    memset(&li, 0, sizeof(li));
    int in_str = 0;
    size_t code_end = len;
    int depth = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (in_str) {
            if (c == '\\' && i + 1 < len) {
                i++;
                continue;
            }
            if (c == '"') in_str = 0;
            continue;
        }
        if (c == '"') {
            in_str = 1;
            continue;
        }
        if (c == ';') {
            code_end = i;
            break;
        }
        if (c == '[' || c == '(') depth++;
        else if ((c == ']' || c == ')') && depth > 0) depth--;
        else if (c == ']' || c == ')') depth--;
        if (!li.rune && !isspace((unsigned char)c)) li.rune = c;
    }
    li.code_end = code_end;
    li.bdelta = depth;
    /* trailing spaces of code */
    size_t e = code_end;
    while (e > 0 && isspace((unsigned char)s[e - 1])) e--;
    /* is the whole code just . or ! ? */
    size_t a = 0;
    while (a < e && isspace((unsigned char)s[a])) a++;
    if (e - a == 1 && (s[a] == '.' || s[a] == '!')) li.closes = 1;
    if (e > 0 && s[e - 1] == ':' && (li.rune == '?' || li.rune == '&' || li.rune == '*'))
        li.opens = 1;
    return li;
}

char *vx_format(const char *src) {
    size_t cap = strlen(src) + 256, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    int depth = 0;
    int bdepth = 0;
    const char *p = src;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t llen = e ? (size_t)(e - p) : strlen(p);
        /* strip trailing \r */
        while (llen > 0 && (p[llen - 1] == '\r')) llen--;
        LineInfo li = scan_line(p, llen);
        /* blank line: keep empty */
        size_t a = 0;
        while (a < llen && isspace((unsigned char)p[a])) a++;
        int blank = (a >= li.code_end);
        int use_depth = depth;
        if (!blank && bdepth == 0 && li.closes && depth > 0) {
            depth--;
            use_depth = depth;
        }
        /* emit */
        if (blank) {
            if (len + 1 >= cap) {
                cap *= 2;
                char *nd = (char *)realloc(out, cap);
                if (!nd) {
                    free(out);
                    return NULL;
                }
                out = nd;
            }
            out[len++] = '\n';
        } else if (bdepth > 0 || li.bdelta < 0) {
            /* inside/around brackets: copy line trimmed of trailing space */
            size_t ce = li.code_end;
            while (ce > 0 && isspace((unsigned char)p[ce - 1])) ce--;
            /* keep original leading whitespace, drop trailing */
            size_t need = ce + (llen - li.code_end) + 1;
            while (len + need + 1 >= cap) {
                cap *= 2;
                char *nd = (char *)realloc(out, cap);
                if (!nd) {
                    free(out);
                    return NULL;
                }
                out = nd;
            }
            memcpy(out + len, p, ce);
            len += ce;
            memcpy(out + len, p + li.code_end, llen - li.code_end);
            len += llen - li.code_end;
            out[len++] = '\n';
        } else {
            /* normal line: fresh indent + code + comment */
            size_t cs = a; /* code start */
            size_t ce = li.code_end;
            while (ce > cs && isspace((unsigned char)p[ce - 1])) ce--;
            size_t cmt = llen - li.code_end; /* includes ;... or spaces */
            /* trim spaces before comment, keep comment text */
            size_t cstart = li.code_end;
            while (cstart < llen && isspace((unsigned char)p[cstart])) cstart++;
            size_t need = (size_t)use_depth * 2 + (ce - cs) + (llen - cstart) + 2;
            if (cstart < llen) need += 2; /* gap before ; */
            while (len + need + 1 >= cap) {
                cap *= 2;
                char *nd = (char *)realloc(out, cap);
                if (!nd) {
                    free(out);
                    return NULL;
                }
                out = nd;
            }
            for (int i = 0; i < use_depth; i++) {
                out[len++] = ' ';
                out[len++] = ' ';
            }
            memcpy(out + len, p + cs, ce - cs);
            len += ce - cs;
            if (cstart < llen) {
                out[len++] = ' ';
                out[len++] = ' ';
                memcpy(out + len, p + cstart, llen - cstart);
                len += llen - cstart;
            }
            out[len++] = '\n';
        }
        if (!blank && bdepth == 0 && li.opens) depth++;
        bdepth += li.bdelta;
        if (bdepth < 0) bdepth = 0;
        if (!e) break;
        p = e + 1;
    }
    if (len + 1 >= cap) {
        char *nd = (char *)realloc(out, len + 1);
        if (!nd) {
            free(out);
            return NULL;
        }
        out = nd;
    }
    out[len] = '\0';
    return out;
}
