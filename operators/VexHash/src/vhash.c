/* VexHash — tiny hashes for Vexel (pure C, no deps).
 * hash_crc/1  — CRC-32 of text; hash_fnv/1 — FNV-1a 32 of text;
 * hash_file/1 — CRC-32 of file bytes, fail if unreadable.
 * Results fit into f64 exactly (32 bit).
 */
#include "operator.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static unsigned crc_tab[256];
static int crc_ready = 0;

static void crc_init(void) {
    if (crc_ready) return;
    for (unsigned i = 0; i < 256; i++) {
        unsigned c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_tab[i] = c;
    }
    crc_ready = 1;
}

static unsigned crc_update(unsigned crc, const unsigned char *s, size_t n) {
    crc_init();
    for (size_t i = 0; i < n; i++)
        crc = crc_tab[(crc ^ s[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

/* hash_crc # text */
static int h_crc(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    unsigned c = crc_update(0xFFFFFFFFu, (const unsigned char *)s, n) ^ 0xFFFFFFFFu;
    *out = api->make_num((double)c);
    return *out ? 0 : 1;
}

/* hash_fnv # text */
static int h_fnv(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(a[0], &s, &n)) return 1;
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    *out = api->make_num((double)h);
    return *out ? 0 : 1;
}

/* hash_file # path */
static int h_file(const VxOpApi *api, VxOpVal **a, int argc, VxOpVal **out) {
    (void)argc;
    const char *s = NULL;
    size_t n = 0;
    if (!api->as_text(a[0], &s, &n) || n == 0 || n > 1024) return 1;
    char path[1025];
    memcpy(path, s, n);
    path[n] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    unsigned crc = 0xFFFFFFFFu;
    unsigned char buf[65536];
    size_t r = 0;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
        crc = crc_update(crc, buf, r);
    fclose(f);
    crc ^= 0xFFFFFFFFu;
    *out = api->make_num((double)crc);
    return *out ? 0 : 1;
}

static const VxOpFuncInfo g_funcs[] = {
    { "hash_crc", 1, h_crc },
    { "hash_fnv", 1, h_fnv },
    { "hash_file", 1, h_file },
};

#ifdef _WIN32
#define VXOP_EXPORT __declspec(dllexport)
#else
#define VXOP_EXPORT
#endif

VXOP_EXPORT const VxOpInfo *vxop_open(const VxOpApi *api) {
    static VxOpInfo info;
    (void)api;
    info.abi_version = VXOP_ABI_VERSION;
    info.name = "VexHash";
    info.version = "1.0.0";
    info.nfuncs = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));
    info.funcs = g_funcs;
    return &info;
}
