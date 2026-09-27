# Shop tools: fmt, lint, test, debug, benches

Small commands around the bench. All read `.vx`, none changes the language.

## `!vex_fmt <file> [-w]`

Normalizes indentation (2 spaces per open block) and strips trailing
space. Brackets/strings/comments are lexed for real, so rune-like
text inside them is left alone. Without `-w` prints to stdout;
with `-w` rewrites the file. Idempotent: formatting twice changes
nothing the second time.

```text
vexel.exe !vex_fmt main.vx -w
```

## `!vex_lint <file>`

Static grumbles without running anything. Exit 0 clean, 1 warnings,
2 broken file:

- `cell '@ x' never read`
- `ask is always wet/dry`, `ask on fail is always dry`
- `circle never turns` (literal 0/negative count)
- `ask with no branches`
- `division by zero is fail` (constant denominator)
- `tool 'len' shadows a built-in`

```text
vexel.exe !vex_lint main.vx
main.vx:2:1: warning: ask is always wet
```

## `!vex_test <file>`

Runs `main` once as setup, then every zero-mark `& test_*` tool.
Wet answer = PASS, dry/fail/error = FAIL. Exit 0/1/2.

```text
& test_sum :
  = 40 + 2 == 42
.
```

```text
vexel.exe !vex_test math_test.vx
PASS test_sum
math_test.vx: 1 passed, 0 failed, 0 skipped
```

## `!vex_debug <file>`

Machine debugger over the real VM: step instructions, peek cells
and stack. Commands: `s[tep] [N]`, `r[un]`, `g[lobals]`,
`v[stack]`, `d[isasm]`, `q[uit]`.

```text
vexel.exe !vex_debug main.vx
Vexel dbg — main.vx
-- main:0000 CONST      0 0   [stack 0, frames 1]
vdb> s
```

## `benches/`

Self-timing benches (need installed VexSYS for `time_ms #`):

- `bench_fib.vx` — fib(22) recursion (~30 ms here)
- `bench_loop.vx` — 200k arithmetic rounds (~50 ms)
- `bench_calls.vx` — 50k summons (~20 ms)

```bat
benches\run_benches.bat
```

The compiler folds pure-number subtrees (`2 + 3 * 4` → one const),
so arithmetic-heavy benches measure the VM, not the parser.
