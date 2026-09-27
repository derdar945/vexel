#ifndef VEXEL_VM_H
#define VEXEL_VM_H

#include "vexel.h"
#include "operator.h"
#include "oploader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VxFrame {
    int tool;      /* -1 = main */
    VxTool *w;
    int ip;
    int base;
    VxValue *locals;
    int loop_depth; /* vm->nloops at call time: circles die with the frame */
} VxFrame;

typedef struct VxLoop {
    long count;
    long it;
} VxLoop;

typedef struct VxVM {
    VxProgram *prog;
    VxValue *stack;
    int sp;
    int capstack;
    VxFrame *frames;
    int nframes;
    int capframes;
    VxLoop *loops;
    int nloops;
    int caploops;
    VxValue *globals;
    void (*show_cb)(const char *line, void *ud);
    void *show_ud;
    VxLoadedOp *ops; /* parallel to prog->ops, loaded lazily */
} VxVM;

void vx_vm_init(VxVM *vm, VxProgram *prog);
void vx_vm_free(VxVM *vm);
int vx_vm_run(VxVM *vm, VxError *err);
/* stepwise driving for the debugger */
int vx_vm_start(VxVM *vm, VxError *err);
int vx_vm_live(VxVM *vm);
/* 1 = still running, 0 = finished, -1 = error */
int vx_vm_step(VxVM *vm, VxError *err);
/* reentrant tool call for operators (see operator.h summon) */
int vx_vm_summon(VxVM *vm, const char *tool, VxOpVal **args, int argc,
                 VxOpVal **out, VxError *err);

#ifdef __cplusplus
}
#endif

#endif
