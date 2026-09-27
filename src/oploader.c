#define _CRT_SECURE_NO_WARNINGS
#include "oploader.h"
#include "opman.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ---------- Core -> operator value bridge ---------- */

static VxOpVal *api_make_num(double n) {
    VxValue *v = (VxValue *)malloc(sizeof(VxValue));
    if (!v) return NULL;
    *v = vx_make_num(n);
    return (VxOpVal *)v;
}

static VxOpVal *api_make_text(const char *s, size_t len) {
    VxValue *v = (VxValue *)malloc(sizeof(VxValue));
    if (!v) return NULL;
    *v = vx_make_text(s ? s : "", len);
    if (v->kind != VXK_TEXT) {
        free(v);
        return NULL;
    }
    return (VxOpVal *)v;
}

static VxOpVal *api_make_fail(void) {
    VxValue *v = (VxValue *)malloc(sizeof(VxValue));
    if (!v) return NULL;
    *v = vx_make_fail();
    return (VxOpVal *)v;
}

static VxOpVal *api_make_vec(void) {
    VxValue *v = (VxValue *)malloc(sizeof(VxValue));
    if (!v) return NULL;
    *v = vx_make_vec();
    if (v->kind != VXK_VEC) {
        free(v);
        return NULL;
    }
    return (VxOpVal *)v;
}

static int api_vec_push(VxOpVal *vec, VxOpVal *item) {
    VxValue *c = (VxValue *)vec, *it = (VxValue *)item;
    if (!c || c->kind != VXK_VEC || !it) return 1;
    VxVec *v = c->as.vec;
    if (v->len + 1 > v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 8;
        VxValue *ni = (VxValue *)realloc(v->items, nc * sizeof(VxValue));
        if (!ni) return 1;
        v->items = ni;
        v->cap = nc;
    }
    vx_retain(it); /* vec shares, caller copy stays owned by caller */
    v->items[v->len++] = *it;
    return 0;
}

static void api_retain(VxOpVal *v) {
    if (v) vx_retain((VxValue *)v);
}

static void api_release(VxOpVal *v) {
    if (!v) return;
    vx_release((VxValue *)v);
    free(v);
}

static VxOpVal *api_clone(VxOpVal *v) {
    if (!v) return NULL;
    VxValue *cp = (VxValue *)malloc(sizeof(VxValue));
    if (!cp) return NULL;
    *cp = *(VxValue *)v;
    vx_retain(cp);
    return (VxOpVal *)cp;
}

static int api_face(VxOpVal *v) {
    if (!v) return VXF_FAIL;
    switch (((VxValue *)v)->kind) {
        case VXK_NUM: return VXF_NUM;
        case VXK_TEXT: return VXF_TEXT;
        case VXK_VEC: return VXF_VEC;
        default: return VXF_FAIL;
    }
}

static int api_as_num(VxOpVal *v, double *out) {
    if (!v || ((VxValue *)v)->kind != VXK_NUM) return 0;
    if (out) *out = ((VxValue *)v)->as.num;
    return 1;
}

static int api_as_text(VxOpVal *v, const char **bytes, size_t *len) {
    if (!v || ((VxValue *)v)->kind != VXK_TEXT) return 0;
    VxText *t = ((VxValue *)v)->as.text;
    if (bytes) *bytes = t->data;
    if (len) *len = t->len;
    return 1;
}

static int api_vec_len(VxOpVal *v, size_t *out) {
    if (!v || ((VxValue *)v)->kind != VXK_VEC) return 0;
    if (out) *out = ((VxValue *)v)->as.vec->len;
    return 1;
}

static VxOpVal *api_vec_get(VxOpVal *v, size_t i) {
    if (!v || ((VxValue *)v)->kind != VXK_VEC) return NULL;
    VxVec *c = ((VxValue *)v)->as.vec;
    if (i >= c->len) return NULL;
    VxValue *cp = (VxValue *)malloc(sizeof(VxValue));
    if (!cp) return NULL;
    *cp = c->items[i];
    vx_retain(cp);
    return (VxOpVal *)cp;
}

static int api_is_true(VxOpVal *v) {
    if (!v) return 0;
    return vx_is_true(*(VxValue *)v) ? 1 : 0;
}

static VxVM *g_vm = NULL;

void *vx_loader_swap_vm(void *vm) {
    void *prev = (void *)g_vm;
    g_vm = (VxVM *)vm;
    return prev;
}

static int api_summon(const char *tool, VxOpVal **args, int argc,
                      VxOpVal **out) {
    if (out) *out = NULL;
    if (!g_vm || !tool || !*tool) return 1;
    if (argc < 0 || argc > 16) return 1;
    VxError err;
    memset(&err, 0, sizeof(err));
    int rc = vx_vm_summon(g_vm, tool, args, argc, out, &err);
    if (rc != 0) {
        fprintf(stderr, "vexel: step '%s' failed: %s\n", tool,
                err.has ? err.msg : "bad summon");
    }
    return rc;
}

static VxOpApi g_api = {
    VXOP_ABI_VERSION,
    api_make_num,
    api_make_text,
    api_make_fail,
    api_make_vec,
    api_vec_push,
    api_retain,
    api_release,
    api_clone,
    api_face,
    api_as_num,
    api_as_text,
    api_vec_len,
    api_vec_get,
    api_is_true,
    api_summon,
};

const VxOpApi *vx_op_api(void) {
    return &g_api;
}

/* ---------- dynamic loading ---------- */

int vx_loader_load(const char *name, VxLoadedOp *op, VxError *err) {
    memset(op, 0, sizeof(*op));
    snprintf(op->name, sizeof(op->name), "%s", name);
    VxManifest m;
    VxError merr;
    memset(&merr, 0, sizeof(merr));
    if (!vx_opm_installed(name, &m, &merr)) {
        vx_error_set(err, 0, 0, "Failed to load Operator: %s\nReason: not installed (try !vex_add %s)",
                     name, name);
        return 0;
    }
    if (strcmp(m.type, "native") != 0) {
        vx_error_set(err, 0, 0, "Failed to load Operator: %s\nReason: type '%s' is not runnable here",
                     name, m.type);
        vx_manifest_free(&m);
        return 0;
    }
    char store[MAX_PATH], path[MAX_PATH];
    vx_store_path(store, sizeof(store));
    snprintf(path, sizeof(path), "%s\\%s\\%s", store, name, m.entry);
    HMODULE dll = LoadLibraryA(path);
    if (!dll) {
        DWORD e = GetLastError();
        vx_error_set(err, 0, 0,
                     "Failed to load Operator: %s\nReason: cannot open %s (code %lu)",
                     name, m.entry, (unsigned long)e);
        vx_manifest_free(&m);
        return 0;
    }
    VxOpOpenFn open =
        (VxOpOpenFn)(void *)GetProcAddress(dll, VXOP_OPEN_NAME);
    if (!open) {
        vx_error_set(err, 0, 0,
                     "Failed to load Operator: %s\nReason: no export %s",
                     name, VXOP_OPEN_NAME);
        FreeLibrary(dll);
        vx_manifest_free(&m);
        return 0;
    }
    const VxOpInfo *info = open(vx_op_api());
    if (!info) {
        vx_error_set(err, 0, 0,
                     "Failed to load Operator: %s\nReason: vxop_open refused",
                     name);
        FreeLibrary(dll);
        vx_manifest_free(&m);
        return 0;
    }
    if (info->abi_version != VXOP_ABI_VERSION) {
        vx_error_set(err, 0, 0,
                     "Operator ABI mismatch: %s speaks ABI %d, core has %d",
                     name, info->abi_version, VXOP_ABI_VERSION);
        FreeLibrary(dll);
        vx_manifest_free(&m);
        return 0;
    }
    if (!info->name || strcmp(info->name, name) != 0 ||
        !info->version || strcmp(info->version, m.version) != 0) {
        vx_error_set(err, 0, 0,
                     "Failed to load Operator: %s\nReason: module identity drift",
                     name);
        FreeLibrary(dll);
        vx_manifest_free(&m);
        return 0;
    }
    op->handle = (void *)dll;
    op->info = info;
    vx_manifest_free(&m);
    return 1;
}

void vx_loader_unload(VxLoadedOp *op) {
    if (op && op->handle) {
        FreeLibrary((HMODULE)op->handle);
        op->handle = NULL;
        op->info = NULL;
    }
}
