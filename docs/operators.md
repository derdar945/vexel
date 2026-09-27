# Operators — extending Vexel without fattening Core

Core stays small: language + compiler + VM + runtime + operator system.
Everything big (windows, network, game) lives in operators.

## Idea

An operator registers verbs that summon with `#` like any tool:

```text
@ VexGUI
> window # "Hi", 400, 300
```

No `import/include/using/require`: `@` already means
"place on the bench", so `@ VexGUI` places the operator.
Attach lines stand on their own (top level), run in order,
and are checked at compile time against the installed manifest.

## Format — operator.vxop

Plain `key = value` lines, `#`/`;` comments allowed:

```text
name = VexGUI
version = 1.0.0
description = Lightweight native windows for Vexel
type = native
vexel_version = 0.1.0
platform = windows
architecture = any
entry = VexGUI.dll
provides = version/0, window/3, show/1
dependencies = VexNet >= 1.0
```

`provides` lists `verb/arity`. `dependencies` lists
`Name [op] [version]` (`>=` by default when a version is given).

## Manager

The store is `<exe-dir>/operators` (`VEXEL_OPS` overrides it,
used by tests). Commands:

```text
!vex_add VexGUI        install by name (deps resolved, cycles refused)
!vex_remove VexGUI     refuse while others need it
!vex_update VexGUI     reinstall from sources
!vex_list              installed
!vex_info VexGUI       manifest + verbs
!vex_search Vex        store + on-disk sources
!vex_operator_install <dir>   install an explicit directory
```

Install validates: manifest, core version (`Operator ABI mismatch`),
platform/arch, entry binary present, dependency tree (missing →
`Missing dependency:`, cycles → `Circular dependency: A -> B -> A`).
Half-done installs are wiped; installing over a dev checkout that
IS the store entry never deletes sources.

## Dependencies

Resolved depth-first, loaded parents-after-children at runtime?
No — simpler and honest: every `@ Name` loads on its own line, in
order; put dependencies first. Version pins use semver compare
(`>=`, `==`, `>`, `<=`, `<`, `!=`).

## Shipped operators

| Operator | Verbs | Doc |
|----------|-------|-----|
| VexGUI | windows, text, button, input, editor, list + events | `docs/vexgui.md`, `operators/VexGUI/README.md` |
| VexFS | read, write, dir, exists, remove | `docs/vexfs.md`, `operators/VexFS/README.md` |
| VexExec | exec (run + catch output) | `docs/vexec.md`, `operators/VexExec/README.md` |
| VexNet | get, serve (plain HTTP) | `docs/vexnet.md`, `operators/VexNet/README.md` |
| VexGame | game canvas, frame loop, keys | `docs/vexgame.md`, `operators/VexGame/README.md` |
| Vex3D | scenes, objects, software 3D | `docs/vex3d.md`, `operators/Vex3D/README.md` |
| VexSYS | os/time/env/path/rand/strings/math battery | `operators/VexSYS/README.md` |

No stubs ship: anything listed here builds, installs and runs.
