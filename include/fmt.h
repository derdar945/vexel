#ifndef VEXEL_FMT_H
#define VEXEL_FMT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Format Vexel source: normalize indentation, strip trailing space.
 * Returns a fresh NUL-terminated buffer (caller frees), or NULL on OOM. */
char *vx_format(const char *src);

#ifdef __cplusplus
}
#endif

#endif
