# LSP: `!vex_lsp` + editor wiring

A JSON-RPC language server lives inside `vexel.exe` (stdio,
Content-Length framing, `src/lsp.c` — no third-party code).

## Server

```text
vexel.exe !vex_lsp
```

Speaks: `initialize` (full-sync, hover, completion, definition,
symbols), `didOpen`/`didChange`, `hover`, `completion`,
`definition`, `documentSymbol`, `shutdown`/`exit`. Every request
recompiles the in-memory bench with the real lexer/parser/compiler,
so diagnostics are exactly the compiler's (`unknown cell`,
arity, unclosed blocks). Hovers cover builtins, bench tools/cells
and attached operator verbs (read from installed manifests).
Malformed input never kills it; unknown methods answer null.

Framing warning, learned the hard way: stdio must be binary mode,
or `\n` becomes `\r\n` and lengths drift — the server sets it
explicitly on Windows.

## Editor side

`vexel-vscode` ships a dependency-free client (`src/lspClient.ts`):
spawns the server, forwards opens/changes, routes hover/completion/
definition/symbols through it, publishes `publishDiagnostics`.
`vexel.lsp: false` disables it; the local providers stay as fallback.
Client↔server interop is covered by `tools/lsp-e2e.js` in the
extension repo (real process, stubbed `vscode` module only).

## Probing by hand

`tools/lsp_probe.py` speaks the protocol from the shell:
initialize → didOpen (broken + clean) → hover/completion/
definition/symbols → shutdown. All green.
