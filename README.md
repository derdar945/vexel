# Vexel

A lightweight programming language with its own rune-based syntax.

```text
> "Hello, vex!"

@ x : 12
? x > 10 :
  > "big"
!
  > "small"
.
* 3 :
  > it
.
& add a b :
  = a + b
.
> add # 2, 3
> risky # 0 ?? 0 - 1
```

## Philosophy

C-like meaning (cells, tools, explicit operations), Python-like lightness
(script top-down, no types, no `;`/`{}`), but a look of its own: every line
starts with a rune mark — `@ & > ? ! . * = # ?? ;`. No `main`, no
`let/if/for/return/try`, no forced typing. Failures are values (`fail`),
rescued with `??`.

## Features

- Tiny C17 core: lexer, parser, compiler, stack VM, runtime, CLI
- Own bytecode (`.vxb`, VXB2; VXB1 still loads) + REPL + disassembler
- `!vex_fmt` / `!vex_lint` / `!vex_test` / `!vex_debug` shop tools
- `!vex_lsp`: JSON-RPC language server inside the binary
- Constant folding, self-timing `benches/`
- Operators: native DLL extensions with a stable C ABI
- VexGUI (real Win32 windows), VexFS, VexExec, VexNet, VexGame, Vex3D, VexSYS
- `!vex_*` CLI for bench, projects, operators and registries
- Self-hosting stage 1: `vex/lex.vx`, a lexer written in Vexel
- HTML docs in `docs/html`, Markdown in `docs/`

## Operators

```text
!vex_add VexGUI
!vex_run examples/gui_hello.vx
```

```text
@ VexGUI
@ w : window # "Hi", 400, 300
& step :
  ? clicked # go :
    > "pressed"
  .
  = open # w
.
> show # w
> run # w, "step"
```

Custom operators: `!vex_operator_new MyOp` → implement against
`include/operator.h` → `!vex_operator_build` → `!vex_add`.

Shipped: **VexGUI** (windows + events), **VexFS** (`read/write/dir/
exists/remove`, UTF-8 paths), **VexExec** (`exec # cmd, timeout` —
run and catch output). Docs: `docs/vexgui.md`, `docs/vexfs.md`,
`docs/vexec.md`.

## VS Code

The [`vexel-vscode`](../vexel-vscode) extension (separate repo) recognises
`.vx`: rune-first highlighting, snippets, `!vex_run`/`!vex_check`/
`!vex_build`/REPL commands, diagnostics from real compiler errors,
operator-aware completion. See `vscode/README.md`.

## Build (Windows)

Needs CMake + MinGW gcc:

```bat
build.bat
vexel.exe !vex_version
```

This builds `vexel.exe` and `operators/VexGUI/VexGUI.dll`.

## Tests

```bat
tests\run_tests.bat
```

Sections: Core, CLI, Operators (+ABI), VexGUI (including a real window:
click, text, close). Operator store is isolated via `VEXEL_OPS`.

## Layout

```text
src/ include/ tests/ examples/ operators/ docs/ tools/ vscode/
README.md VEXEL_DESIGN.md LICENSE .gitignore CMakeLists.txt build.bat
```

## Releases

Local layout via `tools\release.bat` → `release/Vexel/Windows/`
(`vexel.exe`, examples, docs, VexGUI). The `.vsix` is packaged from
the extension repo (`npm run package`). Nothing is uploaded
automatically — create the GitHub release yourself when ready:

```text
git init
git add .
git commit -m "Initial Vexel release"
git branch -M main
git remote add origin <MY_REPOSITORY>
git push -u origin main
```

## Roadmap

- Parser + emitter stages of self-hosting (lexer done)
- More VexGUI widgets (Panel, Canvas, Menu, Dialog, themes)
- Linux/macOS backends (see PORTING.md for the exact surface)
- VS Code Marketplace publishing
