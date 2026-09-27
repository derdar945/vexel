#ifndef VEXEL_LEXER_H
#define VEXEL_LEXER_H

#include "vexel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VxTokKind {
    T_AT,       /* @ */
    T_AMP,      /* & */
    T_QMARK,    /* ? single */
    T_QQ,       /* ?? */
    T_BANG,     /* ! single */
    T_DOT,      /* . */
    T_COLON,    /* : */
    T_STAR,     /* * */
    T_GT,       /* > single */
    T_EQ,       /* = single */
    T_HASH,     /* # */
    T_FAIL, T_IT, T_AND, T_OR, T_NOT,
    T_IDENT, T_NUMBER, T_STRING,
    T_PLUS, T_MINUS, T_SLASH, T_PERCENT,
    T_LT, T_GTE, T_LTE, T_EQEQ, T_NEQ,
    T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET, T_COMMA,
    T_NEWLINE, T_EOF, T_ERR
} VxTokKind;

typedef struct VxToken {
    VxTokKind kind;
    char *lexeme;
    double num;
    int line;
    int col;
} VxToken;

typedef struct VxTokVec {
    VxToken *data;
    int len;
    int cap;
} VxTokVec;

void vx_tokvec_init(VxTokVec *v);
void vx_tokvec_push(VxTokVec *v, VxToken t);
void vx_tokvec_free(VxTokVec *v);
const char *vx_tok_name(VxTokKind k);

bool vx_lex(const char *src, VxTokVec *out, VxError *err);

#ifdef __cplusplus
}
#endif

#endif
