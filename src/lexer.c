#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void vx_tokvec_init(VxTokVec *v) { memset(v, 0, sizeof(*v)); }

void vx_tokvec_push(VxTokVec *v, VxToken t) {
    if (v->len + 1 > v->cap) {
        int nc = v->cap ? v->cap * 2 : 64;
        VxToken *nd = (VxToken *)realloc(v->data, (size_t)nc * sizeof(VxToken));
        if (!nd) return;
        v->data = nd;
        v->cap = nc;
    }
    v->data[v->len++] = t;
}

void vx_tokvec_free(VxTokVec *v) {
    for (int i = 0; i < v->len; i++) free(v->data[i].lexeme);
    free(v->data);
    memset(v, 0, sizeof(*v));
}

const char *vx_tok_name(VxTokKind k) {
    switch (k) {
        case T_AT: return "@"; case T_AMP: return "&";
        case T_QMARK: return "?"; case T_QQ: return "??";
        case T_BANG: return "!"; case T_DOT: return ".";
        case T_COLON: return ":"; case T_STAR: return "*";
        case T_GT: return ">"; case T_EQ: return "=";
        case T_HASH: return "#";
        case T_FAIL: return "fail"; case T_IT: return "it";
        case T_AND: return "and"; case T_OR: return "or"; case T_NOT: return "not";
        case T_IDENT: return "name"; case T_NUMBER: return "number"; case T_STRING: return "text";
        case T_PLUS: return "+"; case T_MINUS: return "-";
        case T_SLASH: return "/"; case T_PERCENT: return "%";
        case T_LT: return "<"; case T_GTE: return ">="; case T_LTE: return "<=";
        case T_EQEQ: return "=="; case T_NEQ: return "!=";
        case T_LPAREN: return "("; case T_RPAREN: return ")";
        case T_LBRACKET: return "["; case T_RBRACKET: return "]";
        case T_COMMA: return ","; case T_NEWLINE: return "newline";
        case T_EOF: return "eof"; default: return "?";
    }
}

static void push_simple(VxTokVec *out, VxTokKind k, int line, int col) {
    VxToken t; memset(&t, 0, sizeof(t));
    t.kind = k; t.line = line; t.col = col;
    vx_tokvec_push(out, t);
}

static int is_ident_start(char c) {
    return (c == '_' || isalpha((unsigned char)c));
}
static int is_ident_char(char c) {
    return (c == '_' || isalnum((unsigned char)c));
}

bool vx_lex(const char *src, VxTokVec *out, VxError *err) {
    vx_tokvec_init(out);
    size_t n = strlen(src);
    size_t i = 0;
    int line = 1, col = 1;
    while (i < n) {
        char c = src[i];
        /* spaces */
        if (c == ' ' || c == '\t' || c == '\r') { i++; col++; continue; }
        if (c == '\n') {
            /* collapse multiples */
            if (out->len == 0 || out->data[out->len - 1].kind != T_NEWLINE)
                push_simple(out, T_NEWLINE, line, col);
            i++; line++; col = 1;
            continue;
        }
        /* comment ; to eol */
        if (c == ';') {
            while (i < n && src[i] != '\n') { i++; }
            continue;
        }
        int l0 = line, c0 = col;
        /* string */
        if (c == '"') {
            i++; col++;
            size_t cap = 32, len = 0;
            char *buf = (char *)malloc(cap);
            if (!buf) { vx_error_set(err, l0, c0, "out of memory"); return false; }
            bool closed = false;
            while (i < n) {
                char d = src[i];
                if (d == '"') { closed = true; i++; col++; break; }
                if (d == '\n') {
                    vx_error_set(err, l0, c0, "text crosses newline, close it with \"");
                    free(buf);
                    return false;
                }
                if (d == '\\') {
                    if (i + 1 >= n) break;
                    char e2 = src[i + 1];
                    char o = e2;
                    if (e2 == 'n') o = '\n';
                    else if (e2 == 't') o = '\t';
                    else if (e2 == 'r') o = '\r';
                    else if (e2 == '"') o = '"';
                    else if (e2 == '\\') o = '\\';
                    if (len + 1 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return false; } buf = nb; }
                    buf[len++] = o;
                    i += 2; col += 2;
                } else {
                    if (len + 1 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return false; } buf = nb; }
                    buf[len++] = d;
                    i++; col++;
                }
            }
            if (!closed) { vx_error_set(err, l0, c0, "text is not closed"); free(buf); return false; }
            VxToken t; memset(&t, 0, sizeof(t));
            t.kind = T_STRING; t.line = l0; t.col = c0;
            t.lexeme = vx_strndup(buf, len);
            free(buf);
            if (!t.lexeme) return false;
            vx_tokvec_push(out, t);
            continue;
        }
        /* number */
        if (isdigit((unsigned char)c) || (c == '.' && i + 1 < n && isdigit((unsigned char)src[i + 1]))) {
            size_t st = i;
            bool dot = false;
            while (i < n && (isdigit((unsigned char)src[i]) || (!dot && src[i] == '.'))) {
                if (src[i] == '.') dot = true;
                i++; col++;
            }
            char *tmp = vx_strndup(src + st, i - st);
            if (!tmp) return false;
            VxToken t; memset(&t, 0, sizeof(t));
            t.kind = T_NUMBER; t.line = l0; t.col = c0;
            t.num = strtod(tmp, NULL);
            free(tmp);
            vx_tokvec_push(out, t);
            continue;
        }
        /* ident / keyword */
        if (is_ident_start(c)) {
            size_t st = i;
            while (i < n && is_ident_char(src[i])) { i++; col++; }
            char *w = vx_strndup(src + st, i - st);
            if (!w) return false;
            VxTokKind k = T_IDENT;
            if (strcmp(w, "fail") == 0) k = T_FAIL;
            else if (strcmp(w, "it") == 0) k = T_IT;
            else if (strcmp(w, "and") == 0) k = T_AND;
            else if (strcmp(w, "or") == 0) k = T_OR;
            else if (strcmp(w, "not") == 0) k = T_NOT;
            VxToken t; memset(&t, 0, sizeof(t));
            t.kind = k; t.line = l0; t.col = c0;
            if (k == T_IDENT) t.lexeme = w;
            else free(w);
            vx_tokvec_push(out, t);
            continue;
        }
        /* two-char ops first */
        if (c == '?' && i + 1 < n && src[i + 1] == '?') {
            push_simple(out, T_QQ, l0, c0); i += 2; col += 2; continue;
        }
        if (c == '=' && i + 1 < n && src[i + 1] == '=') {
            push_simple(out, T_EQEQ, l0, c0); i += 2; col += 2; continue;
        }
        if (c == '!' && i + 1 < n && src[i + 1] == '=') {
            push_simple(out, T_NEQ, l0, c0); i += 2; col += 2; continue;
        }
        if (c == '>' && i + 1 < n && src[i + 1] == '=') {
            push_simple(out, T_GTE, l0, c0); i += 2; col += 2; continue;
        }
        if (c == '<' && i + 1 < n && src[i + 1] == '=') {
            push_simple(out, T_LTE, l0, c0); i += 2; col += 2; continue;
        }
        switch (c) {
            case '@': push_simple(out, T_AT, l0, c0); i++; col++; break;
            case '&': push_simple(out, T_AMP, l0, c0); i++; col++; break;
            case '?': push_simple(out, T_QMARK, l0, c0); i++; col++; break;
            case '!': push_simple(out, T_BANG, l0, c0); i++; col++; break;
            case '.': push_simple(out, T_DOT, l0, c0); i++; col++; break;
            case ':': push_simple(out, T_COLON, l0, c0); i++; col++; break;
            case '*': push_simple(out, T_STAR, l0, c0); i++; col++; break;
            case '>': push_simple(out, T_GT, l0, c0); i++; col++; break;
            case '=': push_simple(out, T_EQ, l0, c0); i++; col++; break;
            case '#': push_simple(out, T_HASH, l0, c0); i++; col++; break;
            case '+': push_simple(out, T_PLUS, l0, c0); i++; col++; break;
            case '-': push_simple(out, T_MINUS, l0, c0); i++; col++; break;
            case '/': push_simple(out, T_SLASH, l0, c0); i++; col++; break;
            case '%': push_simple(out, T_PERCENT, l0, c0); i++; col++; break;
            case '<': push_simple(out, T_LT, l0, c0); i++; col++; break;
            case '(': push_simple(out, T_LPAREN, l0, c0); i++; col++; break;
            case ')': push_simple(out, T_RPAREN, l0, c0); i++; col++; break;
            case '[': push_simple(out, T_LBRACKET, l0, c0); i++; col++; break;
            case ']': push_simple(out, T_RBRACKET, l0, c0); i++; col++; break;
            case ',': push_simple(out, T_COMMA, l0, c0); i++; col++; break;
            default: {
                vx_error_set(err, l0, c0, "strange mark '%c'", c);
                return false;
            }
        }
    }
    VxToken t; memset(&t, 0, sizeof(t));
    t.kind = T_EOF; t.line = line; t.col = col;
    vx_tokvec_push(out, t);
    return true;
}
