#ifndef VEXEL_OPLOADER_H
#define VEXEL_OPLOADER_H

#include "vexel.h"
#include "operator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VxLoadedOp {
    char name[64];
    void *handle; /* HMODULE */
    const VxOpInfo *info;
} VxLoadedOp;

/* the singleton Core->operator API table */
const VxOpApi *vx_op_api(void);

/* load installed operator by name (store). 1 ok, 0 fail + err. */
int vx_loader_load(const char *name, VxLoadedOp *op, VxError *err);
void vx_loader_unload(VxLoadedOp *op);
/* current VM for operator summon (save/restore around calls) */
void *vx_loader_swap_vm(void *vm);

#ifdef __cplusplus
}
#endif

#endif
