#ifndef VEXEL_LINT_H
#define VEXEL_LINT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Lint Vexel source. Prints file:line:col: warnings to stdout.
 * Returns: 0 clean, 1 warnings, 2 broken file. */
int vx_lint(const char *src, const char *path);

#ifdef __cplusplus
}
#endif

#endif
