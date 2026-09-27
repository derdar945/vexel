#ifndef VEXEL_OPERATOR_H
#define VEXEL_OPERATOR_H

/* Vexel Native Operator ABI v1.
 *
 * A native operator is a DLL that exports exactly one symbol:
 *
 *     const VxOpInfo *vxop_open(const VxOpApi *api);
 *
 * The Core passes an api table; the operator must ONLY touch Vexel
 * values through it. Ownership rules:
 *  - args are owned by the Core. Do NOT release them.
 *  - *out must be a FRESH value built via api constructors or
 *    api->clone (ownership moves to the Core), or NULL (= fail).
 *    Never hand back an args pointer itself: it borrows VM memory.
 *  - retain() only values the operator stores past the call.
 *  - every retained stored value must be released before/at unload.
 *  - func must return 0 on success, nonzero to raise fail.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXOP_ABI_VERSION 1
#define VXOP_OPEN_NAME "vxop_open"

typedef enum VxOpFace {
    VXF_FAIL = 0,
    VXF_NUM = 1,
    VXF_TEXT = 2,
    VXF_VEC = 3
} VxOpFace;

typedef struct VxOpVal VxOpVal; /* opaque: actually a Core VxValue */

typedef struct VxOpApi {
    int abi_version; /* VXOP_ABI_VERSION */
    VxOpVal *(*make_num)(double n);
    VxOpVal *(*make_text)(const char *s, size_t len);
    VxOpVal *(*make_fail)(void);
    VxOpVal *(*make_vec)(void);
    /* vec_push takes ownership of item on success (0), else nonzero.
       On failure the caller still owns item. */
    int (*vec_push)(VxOpVal *vec, VxOpVal *item);
    void (*retain)(VxOpVal *v);
    void (*release)(VxOpVal *v);
    /* clone: fresh copy sharing payload (retained). NULL on error. */
    VxOpVal *(*clone)(VxOpVal *v);
    int (*face)(VxOpVal *v); /* VxOpFace */
    /* as_num: 1 on number, else 0. */
    int (*as_num)(VxOpVal *v, double *out);
    /* as_text: borrowed bytes, valid while v is alive. 1 ok, else 0. */
    int (*as_text)(VxOpVal *v, const char **bytes, size_t *len);
    int (*vec_len)(VxOpVal *v, size_t *out); /* 1 ok, else 0 */
    /* vec_get: retained copy, caller must release. NULL on error. */
    VxOpVal *(*vec_get)(VxOpVal *v, size_t i);
    /* wet test: 1 wet, 0 dry. */
    int (*is_true)(VxOpVal *v);
    /* summon a Vexel tool by name (event steps). Takes ownership of
       args (releases them). *out is fresh and owned by the caller.
       0 ok, nonzero = fail. Only valid inside an operator call. */
    int (*summon)(const char *tool, VxOpVal **args, int argc,
                  VxOpVal **out);
} VxOpApi;

typedef int (*VxOpFunc)(const VxOpApi *api, VxOpVal **args, int argc,
                        VxOpVal **out);

typedef struct VxOpFuncInfo {
    const char *name; /* e.g. "window" (called as window # ...) */
    int arity;
    VxOpFunc func;
} VxOpFuncInfo;

typedef struct VxOpInfo {
    int abi_version; /* VXOP_ABI_VERSION */
    const char *name;    /* must match operator.vxop */
    const char *version; /* semver, must match operator.vxop */
    int nfuncs;
    const VxOpFuncInfo *funcs;
} VxOpInfo;

typedef const VxOpInfo *(*VxOpOpenFn)(const VxOpApi *api);

#ifdef __cplusplus
}
#endif

#endif
