# Changelog — Vexel

## 0.2.0

Language change:

- Qualified summons: `Op.verb # args` calls into one named operator.
  Unqualified keeps the old rule (first attached provider wins).
  New check errors: `place @ X first`, `X has no verb Y`.
  Docs: `docs/language.md`, `!vex_help`.

New operators (all native, pure C or user32/gdi32 only):

- VexJSON 1.0.0 — validate, parse, dotted-path get, len
- VexCSV 1.0.0 — quoted-field CSV into vecs of rows
- VexKV 1.0.0 — file-backed string map
- VexDlg 1.0.0 — native message boxes
- VexClip 1.0.0 — clipboard text
- VexHash 1.0.0 — CRC-32 and FNV-1a for text and files
- VexUI 1.1.0 — dark UI: cards, pill buttons, progress, drag
  slider, dark list, pill tabs (78 KB of a 150 KB budget)
- VexTBL 1.0.0 — sort/filter vec-of-rows tables, header stays
- VexPLOT 1.0.0 — ASCII bar/line charts from vecs of numbers

Tests:

- `tests/operators/t_op_ns.vx` (+2 negative `e_ns_*`): qualified
  dispatch, unqualified first-wins documented as `2.0.0/1.0.0/2.0.0
- `gui e2e` fixed: bench path with spaces split argv on Windows
- Full suite green: core, CLI, operators, VexGUI, selfhost

## 0.1.0

Initial marks bench: cells, shows, asks, circles, tools, rescue.
VM, compiler, fmt/lint/test/debug, LSP, operator manager with
registries, first-party operators (GUI, Game, 3D, GL, FS, Exec,
Net, SYS), selfhost lexer (`vex/lex.vx`), VS Code notes.
