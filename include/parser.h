#ifndef VEXEL_PARSER_H
#define VEXEL_PARSER_H

#include "lexer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VxExprKind {
    E_NUM, E_TEXT, E_FAIL, E_IT, E_VAR,
    E_UNARY, E_BINARY, E_CALL, E_RESCUE, E_VEC
} VxExprKind;

typedef enum VxBinOp {
    B_ADD, B_SUB, B_MUL, B_DIV, B_MOD,
    B_GT, B_LT, B_GTE, B_LTE, B_EQ, B_NEQ,
    B_AND, B_OR
} VxBinOp;

typedef enum VxUnOp {
    U_NEG, U_NOT
} VxUnOp;

typedef struct VxExpr VxExpr;
struct VxExpr {
    VxExprKind kind;
    int line, col;
    union {
        double num;
        char *text;
        char *varname;
        struct { VxUnOp op; VxExpr *rhs; } unary;
        struct { VxBinOp op; VxExpr *lhs; VxExpr *rhs; } binary;
        struct { char *name; VxExpr **args; int nargs; } call;
        struct { VxExpr *lhs; VxExpr *rhs; } rescue;
        struct { VxExpr **items; int nitems; } vec;
    } u;
};

typedef enum VxBeatKind {
    B_CELL, B_SHOW, B_ASK, B_LOOP, B_TOOL, B_GIVE, B_EXPR, B_ATTACH
} VxBeatKind;

typedef struct VxBeat VxBeat;
struct VxBeat {
    VxBeatKind kind;
    int line, col;
    VxBeat *next;
    union {
        struct { char *name; VxExpr *expr; } cell;
        struct { VxExpr *expr; } show;
        struct { VxExpr *cond; VxBeat *thenb; VxBeat *elseb; } ask;
        struct { VxExpr *count; VxBeat *body; } loop;
        struct { char *name; char **params; int nparams; VxBeat *body; } tool;
        struct { VxExpr *expr; } give;
        struct { VxExpr *expr; } expr;
        struct { char *name; } attach; /* `@ Name` — place operator on bench */
    } u;
};

typedef struct VxAst {
    VxBeat *head;
} VxAst;

void vx_ast_free(VxAst *a);
void vx_expr_free(VxExpr *e);
void vx_beats_free(VxBeat *b);

bool vx_parse(VxTokVec *toks, VxAst *out, VxError *err);

#ifdef __cplusplus
}
#endif

#endif
