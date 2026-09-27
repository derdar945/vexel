#ifndef VEXEL_OPMAN_H
#define VEXEL_OPMAN_H

#include "vexel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* operator.vxop manifest */
typedef struct VxOpProvide {
    char name[64];
    int arity;
} VxOpProvide;

typedef struct VxOpDep {
    char name[64];
    char op[3]; /* "", "=", "==", ">=", ">", "<=", "<", "!=" */
    char ver[32];
} VxOpDep;

typedef struct VxManifest {
    char name[64];
    char version[32];
    char description[256];
    char type[16]; /* native | vexel */
    char vexel_version[32]; /* minimum Core version */
    char platform[16]; /* any | windows */
    char arch[16];     /* any | x64 | x86 */
    char entry[128];   /* dll file name (native) */
    VxOpProvide *provides;
    int nprovides;
    VxOpDep *deps;
    int ndeps;
} VxManifest;

void vx_manifest_free(VxManifest *m);
/* read <dir>/operator.vxop */
int vx_manifest_read(const char *dir, VxManifest *m, VxError *err);

/* store: <exe>/operators, override with VEXEL_OPS env */
int vx_store_path(char *buf, size_t cap);
/* find installable source: ./operators/<name>, ./<name> */
int vx_source_dir(const char *name, char *buf, size_t cap);

int vx_semver_cmp(const char *a, const char *b); /* -1|0|1 */
int vx_dep_ok(const char *have, const char *op, const char *want);

/* manager (all print their own VEXEL-styled output, 0 ok) */
int vx_opm_list(void);
int vx_opm_info(const char *name);
int vx_opm_search(const char *term);
int vx_opm_add(const char *name);    /* install by name from sources */
int vx_opm_remove(const char *name);
int vx_opm_update(const char *name);
int vx_opm_install_path(const char *path); /* install local dir */
/* used by compiler: read installed manifest */
int vx_opm_installed(const char *name, VxManifest *m, VxError *err);
/* registries: extra roots (local dirs or http bases) for !vex_add */
int vx_opm_registry_add(const char *root);
int vx_opm_registry_list(void);
int vx_opm_registry_remove(const char *root);

/* scaffolding + local build */
int vx_opm_new_project(const char *name);
int vx_opm_new_operator(const char *name);
int vx_opm_build_operator(const char *dir);

#ifdef __cplusplus
}
#endif

#endif
