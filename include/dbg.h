#ifndef VEXEL_DBG_H
#define VEXEL_DBG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Interactive machine debugger: step instructions, peek stack/globals.
 * Commands: s[tep] [N], r[un], g[lobals], v[alue stack], d[isasm], q[uit].
 * Returns process exit code. */
int vx_debug_file(const char *src, const char *path);

#ifdef __cplusplus
}
#endif

#endif
