#ifndef VEXEL_H
#define VEXEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VEXEL_VERSION "0.1.0"
#define VEXEL_MAGIC "VXB1"
#define VEXEL_MAGIC2 "VXB2"

/* ============ value: one entity, four faces ============ */

typedef enum VxKind {
    VXK_FAIL = 0,
    VXK_NUM = 1,
    VXK_TEXT = 2,
    VXK_VEC = 3
} VxKind;

typedef struct VxText VxText;
typedef struct VxVec VxVec;
typedef struct VxValue VxValue;

struct VxText {
    int refs;
    size_t len;
    char data[];
};

struct VxVec {
    int refs;
    size_t len;
    size_t cap;
    VxValue *items;
};

struct VxValue {
    VxKind kind;
    union {
        double num;
        VxText *text;
        VxVec *vec;
    } as;
};

extern const VxValue VX_FAIL_VAL;

VxValue vx_make_num(double n);
VxValue vx_make_fail(void);
VxValue vx_make_text(const char *s, size_t len);
VxValue vx_make_text_cstr(const char *s);
VxValue vx_make_vec(void);
void vx_retain(VxValue *v);
void vx_release(VxValue *v);
void vx_value_free(VxValue *v);
bool vx_is_fail(VxValue v);
bool vx_is_true(VxValue v);
char *vx_repr(VxValue v);

VxValue vx_add(VxValue a, VxValue b);
VxValue vx_sub(VxValue a, VxValue b);
VxValue vx_mul(VxValue a, VxValue b);
VxValue vx_div(VxValue a, VxValue b);
VxValue vx_mod(VxValue a, VxValue b);
VxValue vx_neg(VxValue a);
VxValue vx_cmp_gt(VxValue a, VxValue b);
VxValue vx_cmp_lt(VxValue a, VxValue b);
VxValue vx_cmp_eq(VxValue a, VxValue b);
VxValue vx_cmp_neq(VxValue a, VxValue b);
VxValue vx_cmp_gte(VxValue a, VxValue b);
VxValue vx_cmp_lte(VxValue a, VxValue b);
VxValue vx_logic_and(VxValue a, VxValue b);
VxValue vx_logic_or(VxValue a, VxValue b);
VxValue vx_logic_not(VxValue a);
long vx_loop_count(VxValue v);

/* ============ bytecode ============ */

typedef enum VxOp {
    VX_CONST = 0,
    VX_FAIL_OP,
    VX_IT_OP,
    VX_LOAD_G,
    VX_LOAD_L,
    VX_STORE_G,
    VX_STORE_L,
    VX_ADD,
    VX_SUB,
    VX_MUL,
    VX_DIV,
    VX_MOD,
    VX_GT,
    VX_LT,
    VX_EQ,
    VX_NEQ,
    VX_GTE,
    VX_LTE,
    VX_AND,
    VX_OR,
    VX_NOT,
    VX_NEG,
    VX_VEC_OP,
    VX_SHOW,
    VX_JUMP,
    VX_JUMP_DRY,
    VX_JUMP_NF,
    VX_DROP,
    VX_LOOP_ENTER,
    VX_LOOP_NEXT,
    VX_CALL,
    VX_NATIVE,
    VX_GIVE,
    VX_HALT,
    VX_ATTACH,  /* a = op index: place operator on bench */
    VX_CALL_OP  /* a = callref index: summon operator tool */
} VxOp;

typedef struct VxInstr {
    uint8_t op;
    int32_t a;
    int32_t b;
} VxInstr;

typedef struct VxTool {
    char *name;
    char **locals;
    int nlocals;
    int nparams;
    int cap_locals;
    VxInstr *code;
    int ncode;
    int capcode;
} VxTool;

/* operators placed on the bench (`@ Name`) */
typedef struct VxOpFuncUse {
    char name[64];
    int arity;
} VxOpFuncUse;

typedef struct VxOpUse {
    char name[64];
    VxOpFuncUse *funcs;
    int nfuncs;
} VxOpUse;

/* resolved operator summons: name # args */
typedef struct VxOpCallRef {
    int op;
    int func;
    int argc;
} VxOpCallRef;

typedef struct VxProgram {
    char **globals;
    int nglobals;
    int cap_globals;
    VxValue *consts;
    int nconsts;
    int cap_consts;
    VxTool *tools;
    int ntools;
    int cap_tools;
    VxOpUse *ops;
    int nops;
    int cap_ops;
    VxOpCallRef *callrefs;
    int ncallrefs;
    int cap_callrefs;
    VxTool main;
} VxProgram;

void vx_program_init(VxProgram *p);
void vx_program_free(VxProgram *p);
int vx_program_add_global(VxProgram *p, const char *name);
int vx_program_find_global(VxProgram *p, const char *name);
int vx_program_add_const(VxProgram *p, VxValue v);
int vx_program_add_op(VxProgram *p, const char *name); /* idx, adds if new */
int vx_program_find_op(VxProgram *p, const char *name);
int vx_op_add_func(VxProgram *p, int opidx, const char *name, int arity);
int vx_program_add_callref(VxProgram *p, int op, int func, int argc);
int vx_tool_add_local(VxTool *w, const char *name);
int vx_tool_find_local(VxTool *w, const char *name);
void vx_tool_emit(VxTool *w, VxOp op, int32_t a, int32_t b);
void vx_tool_free(VxTool *w);

typedef enum VxNative {
    VXN_LEN = 0,
    VXN_AT = 1,
    VXN_TYPE = 2,
    VXN_STR = 3,
    VXN_COUNT
} VxNative;
const char *vx_native_name(int n);
int vx_native_lookup(const char *name);

typedef struct VxError {
    bool has;
    char msg[1024];
    int line;
    int col;
} VxError;

void vx_error_set(VxError *e, int line, int col, const char *fmt, ...);

char *vx_strdup(const char *s);
char *vx_strndup(const char *s, size_t n);

#ifdef __cplusplus
}
#endif

#endif
