#ifndef VEXEL_TEST_H
#define VEXEL_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Run a test bench: executes main once (setup), then every zero-mark
 * `& test_*` tool. Wet answer = PASS, dry/fail/error = FAIL.
 * Returns: 0 all passed, 1 some failed, 2 broken file. */
int vx_test_file(const char *src, const char *path);

#ifdef __cplusplus
}
#endif

#endif
