#ifndef VEXEL_COMPILER_H
#define VEXEL_COMPILER_H

#include "parser.h"

#ifdef __cplusplus
extern "C" {
#endif

bool vx_compile(VxAst *ast, VxProgram *out, VxError *err);
void vx_disasm(const VxProgram *p);
const char *vx_op_name(int op);
bool vx_save(const VxProgram *p, const char *path, VxError *err);
bool vx_load(const char *path, VxProgram *out, VxError *err);

#ifdef __cplusplus
}
#endif

#endif
